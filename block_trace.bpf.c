#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "block_trace.h"
#include "common_bpf.h"
char LICENSE[] SEC("license") = "Dual BSD/GPL";

#define REQ_OP_BITS	8
#define REQ_OP_MASK	((1 << REQ_OP_BITS) - 1)
#define bio_op(bi_opf) \
	(bi_opf & REQ_OP_MASK)
static inline bool op_is_write(unsigned int op)
{
	return (op & 1);
}
#define bio_data_dir(bi_opf) \
	(op_is_write(bio_op(bi_opf)) ? WRITE : READ)

char tcommon_name[16] = "None";
int tpid_block_trace = 0;
int using_pid_filter = 0;
int using_common_filter = 0;

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} blk_log SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256 * 128);
	__type(key, u64);
	__type(value, time_stramp);
} bio_pass SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256);
	__type(key, u64);
	__type(value,struct rq_trace_event);
} rq_cache SEC(".maps");


//only do enter log
SEC("kprobe/blk_mq_submit_bio")
int BPF_KPROBE(kpblk_mq_submit_bio,struct bio* bio)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_block_trace)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
    struct bio_enter* enter_log;
    enter_log = bpf_ringbuf_reserve(&blk_log,sizeof(*enter_log),0);
	u64 now = bpf_ktime_get_ns();
	u64 bio_ptr = (unsigned long long)bio;
    if(enter_log){
        enter_log->bi_private = (unsigned long long)BPF_CORE_READ(bio,bi_private);
        enter_log->tp = now;
        enter_log->bio_ptr = bio_ptr;
        bpf_ringbuf_submit(enter_log,0);
    }
	bpf_map_update_elem(&bio_pass,&bio_ptr,&now,BPF_ANY);
	return 0;
}

SEC("kprobe/__rq_qos_track")
int BPF_KPROBE(kp__rq_qos_track,struct rq_qos *rqos, struct request *rq, struct bio *bio)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_block_trace)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
    //check if request is commited at queue_rq
    u64 key = (unsigned long long)rq;
    struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
    if(log){
        //has_rq means not commit at queue_rq, may merge at sched or fail, now is recyled
        bpf_map_delete_elem(&rq_cache,&key);
    }
    struct rq_trace_event trace;
    __builtin_memset(&trace, 0, sizeof(struct rq_trace_event));
    //use to match
	u64 bio_ptr = (unsigned long long)bio;
	time_stramp* bio_enter_time_ptr = bpf_map_lookup_elem(&bio_pass,&bio_ptr);
	if(bio_enter_time_ptr){
		trace.bio_enter = *bio_enter_time_ptr;
		trace.rq_start = bpf_ktime_get_ns();
		unsigned int bi_opf = BPF_CORE_READ(bio,bi_opf);
		trace.rq_sub.io_type = bio_data_dir(bi_opf);
		bpf_get_current_comm(trace.common_name,16);
    	//add to rq_cache
    	bpf_map_update_elem(&rq_cache,&key,&trace,BPF_NOEXIST);
	}
	return 0;
}

SEC("kprobe/blk_add_rq_to_plug")
int BPF_KPROBE(kpblk_add_rq_to_plug,struct blk_plug *plug, struct request *rq)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_sub.plug = bpf_ktime_get_ns();
	}else{
		return -1;
	}
	return 0;
}


SEC("kprobe/blk_mq_request_bypass_insert")
int BPF_KPROBE(kpblk_mq_request_bypass_insert,struct request *rq, bool at_head,
				  bool run_queue)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_sub.sched_path.sched = bpf_ktime_get_ns();
		log->rq_sub.s_type = BYPASS;
	}else{
		return -1;
	}
	return 0;
}


SEC("kprobe/dd_insert_request")
int BPF_KPROBE(kpdd_insert_request,struct blk_mq_hw_ctx *hctx, struct request *rq,
			      bool at_head)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
        log->rq_sub.s_type = MQ_DEADLINE;
	}else{
		return -1;
	}
	return 0;
}

SEC("kprobe/bfq_insert_request")
int BPF_KPROBE(kpbfq_insert_request,struct blk_mq_hw_ctx *hctx, struct request *rq, bool at_head)
{
    u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
        log->rq_sub.s_type = BFQ;
	}else{
		return -1;
	}
	return 0;
}

//only when enter iosched
//if enter bfq_insert_request/dd_insert_request before, ->bfq or mq_deadline
//else main kyber
SEC("kprobe/blk_mq_sched_request_inserted")
int BPF_KPROBE(kpblk_mq_sched_request_inserted,struct request *rq)
{

	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_sub.sched_path.sched = bpf_ktime_get_ns();
        if(log->rq_sub.s_type != MQ_DEADLINE && log->rq_sub.s_type != BFQ){
            //must by, and kyber never join
            log->rq_sub.s_type = KYBER;
        }
	}else{
		return -1;
	}
	return 0;
}


//bfq_dispatch_request
//dd_dispatch_request
//kyber_dispatch_request
SEC("kretprobe/bfq_dispatch_request")
int BPF_KRETPROBE(krpbfq_dispatch_request, struct request *rq)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
        log->rq_sub.d_type = BFQ_;
        log->rq_sub.sched_path.dispatch = bpf_ktime_get_ns();
	}else{
		return -1;
	}
    return 0;
}

SEC("kretprobe/dd_dispatch_request")
int BPF_KRETPROBE(krpdd_dispatch_request, struct request *rq)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
        log->rq_sub.d_type = MQ_DEADLINE_;
        log->rq_sub.sched_path.dispatch = bpf_ktime_get_ns();
	}else{
		return -1;
	}
    return 0;
}


SEC("kretprobe/kyber_dispatch_request")
int BPF_KRETPROBE(krkyber_dispatch_request, struct request *rq)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
        log->rq_sub.d_type = KYBER_;
        log->rq_sub.sched_path.dispatch = bpf_ktime_get_ns();
	}else{
		return -1;
	}
	return 0;
}

SEC("kretprobe/blk_mq_dequeue_from_ctx")
int BPF_KRETPROBE(krpblk_mq_dequeue_from_ctx, struct request *rq)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
        log->rq_sub.d_type = DEQUEUE_CTX_;
        log->rq_sub.sched_path.dispatch = bpf_ktime_get_ns();
	}else{
		return -1;
	}
	return 0;
}


SEC("kprobe/__blk_mq_issue_directly")
int BPF_KPROBE(kp__blk_mq_issue_directly,struct blk_mq_hw_ctx *hctx,
					    struct request *rq,
					    blk_qc_t *cookie, bool last)
{
	u64 key = (unsigned long long)rq;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
        log->rq_sub.direct_path.direct = bpf_ktime_get_ns();
        log->rq_sub.s_type = NO_SCHEDU;
        log->rq_sub.d_type = DIRECT_DISPATCH;
	}else{
		return -1;
	}
    return 0;
}

SEC("kprobe/nvme_queue_rq")
int BPF_KPROBE(kpnvme_queue_rq,struct blk_mq_hw_ctx *hctx,const struct blk_mq_queue_data *bd)
{
    u64 now = bpf_ktime_get_ns();
    u64 key = (unsigned long long)BPF_CORE_READ(bd,rq);
    struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
    if(log){
        log->rq_sub.queue_rq = now;
		log->rq_comp.drive = NVME;
		__builtin_memcpy(log->disk_name,BPF_CORE_READ(bd,rq,rq_disk,disk_name),32);
    }else{
        return -1;
    }
	return 0;
}


SEC("kprobe/scsi_queue_rq")
int BPF_KPROBE(kpscsi_queue_rq,struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
    u64 now = bpf_ktime_get_ns();
    u64 key = (unsigned long long)BPF_CORE_READ(bd,rq);
    struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
    if(log){
        log->rq_sub.queue_rq = now;
		log->rq_comp.drive = SCSI;
		__builtin_memcpy(log->disk_name,BPF_CORE_READ(bd,rq,rq_disk,disk_name),32);
    }else{
        return -1;
    }
	return 0;
}

SEC("kprobe/scsi_end_request")
int BPF_KPROBE(kpscsi_end_request,struct request *req, blk_status_t error,
		unsigned int bytes)
{
	u64 key = (unsigned long long)req;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.scsi_path.scsi_end = bpf_ktime_get_ns();
	}else{
		return -1;
	}
	return 0;
}


//dma_unmap_page nvme_unmap_data
SEC("kprobe/nvme_complete_rq")
int BPF_KPROBE(kpnvme_complete_rq,struct request *req)
{
	u64 key = (unsigned long long)req;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.nvme_path.nvme_complete_rq = bpf_ktime_get_ns();
	}else{
		return -1;
	}
	return 0;
}


SEC("kprobe/blk_account_io_done")
int BPF_KPROBE(kpblk_account_io_done,struct request *req, u64 now)
{
	u64 key = (unsigned long long)req;
	struct rq_trace_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.account_io = now;
		//write meta data to log
		log->rq_comp.sector = (unsigned long long)BPF_CORE_READ(req,__sector);
		log->rq_comp.stats_sectors = BPF_CORE_READ(req,stats_sectors);
		log->rq_comp.nr_phys_segments = BPF_CORE_READ(req,nr_phys_segments);
	}else{
		return -1;
	}
	struct rq_trace_event* event = bpf_ringbuf_reserve(&blk_log,sizeof(struct rq_trace_event),0);
	if(event){
		__builtin_memcpy(event,log,sizeof(struct rq_trace_event));
		bpf_ringbuf_submit(event,0);
		bpf_map_delete_elem(&rq_cache,&key);
	}else{
		return -1;
	}
	return 0;
}















