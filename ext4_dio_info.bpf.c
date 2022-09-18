#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "ext4_dio_info.h"
#include "common_bpf.h"
char LICENSE[] SEC("license") = "Dual BSD/GPL";

char tcommon_name[16] = "None";
volatile unsigned long long dio_write_start = 0;
volatile unsigned long long dio_read_start = 0;
int tpid_ext4_dio = 0;
int using_pid_filter = 0;
int using_common_filter = 0;
int using_inode_filter = 0;

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} ext4_dio_log SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16);
	__type(key, unsigned long);//inode
	__type(value,bool);
} file_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256);
	__type(key, u64);//dio_ptr
	__type(value,u64);//dio_start_block_time as uid
} dio_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024*8);
	__type(key, u64);
	__type(value,struct simple_dio_event);
} event_cache SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 512);
	__type(key, u64);//request_ptr
	__type(value,struct rq_info_event);
} rq_cache SEC(".maps");

SEC("kprobe/new_sync_write")
int BPF_KPROBE(kpnew_sync_write,struct file *filp, const char *buf, size_t len, loff_t *ppos)
{
	//filter
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
	if(using_inode_filter){
		//get inode from file
		unsigned long inode_id = BPF_CORE_READ(filp,f_inode,i_ino);
		unsigned long* count_ptr = bpf_map_lookup_elem(&file_filter,&inode_id);
		if(count_ptr){
			dio_write_start = bpf_ktime_get_ns();
		}
		return 0;
	}
	dio_write_start = bpf_ktime_get_ns();
	return 0;
}

SEC("kprobe/new_sync_read")
int BPF_KPROBE(kpnew_sync_read,struct file *filp, char *buf, size_t len, loff_t *ppos)
{
	//filter
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
	if(using_inode_filter){
		//get inode from file
		unsigned long inode_id = BPF_CORE_READ(filp,f_inode,i_ino);
		unsigned long* count_ptr = bpf_map_lookup_elem(&file_filter,&inode_id);
		if(count_ptr){
			dio_read_start = bpf_ktime_get_ns();
		}
		return 0;
	}
	dio_read_start = bpf_ktime_get_ns();
	return 0;
}

SEC("kprobe/ext4_dio_write_iter")
int BPF_KPROBE(kpext4_dio_write_iter,struct kiocb *iocb, struct iov_iter *from)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
	unsigned long inode_id = BPF_CORE_READ(iocb,ki_filp,f_inode,i_ino);
	if(using_inode_filter){
		unsigned long* count_ptr = bpf_map_lookup_elem(&file_filter,&inode_id);
		if(count_ptr == NULL){
			return 0;
		}
	}
	//is able to count
	struct simple_dio_event dio_simple;
	my_memset_zero(&dio_simple,sizeof(struct simple_dio_event));
	dio_simple.start_dio = dio_write_start;
	dio_simple.start_ext4 = bpf_ktime_get_ns();
	dio_simple.inode_id = inode_id;
	unsigned char* small_name = BPF_CORE_READ(iocb,ki_filp,f_path.dentry,d_iname);
	__builtin_memcpy(&dio_simple.filename,small_name,16);
	dio_simple.type = D_WRITE;
	u64 iocb_ptr = (u64)iocb;
	int ret = bpf_map_update_elem(&event_cache,&iocb_ptr,&dio_simple,BPF_NOEXIST);
	if(0 != ret){
		bpf_printk("same iocb_ptr unk");
	}
	return 0;
}

SEC("kprobe/ext4_dio_read_iter")
int BPF_KPROBE(kpext4_dio_read_iter,struct kiocb *iocb, struct iov_iter *from)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
	unsigned long inode_id = BPF_CORE_READ(iocb,ki_filp,f_inode,i_ino);
	if(using_inode_filter){
		unsigned long* count_ptr = bpf_map_lookup_elem(&file_filter,&inode_id);
		if(count_ptr == NULL){
			return 0;
		}
	}
	//is able to count
	struct simple_dio_event dio_simple;
	my_memset_zero(&dio_simple,sizeof(struct simple_dio_event));
	dio_simple.start_dio = dio_read_start;
	dio_simple.start_ext4 = bpf_ktime_get_ns();
	dio_simple.inode_id = inode_id;
	unsigned char* small_name = BPF_CORE_READ(iocb,ki_filp,f_path.dentry,d_iname);
	__builtin_memcpy(&dio_simple.filename,small_name,16);
	dio_simple.type = D_READ;
	u64 iocb_ptr = (u64)iocb;
	int ret = bpf_map_update_elem(&event_cache,&iocb_ptr,&dio_simple,BPF_NOEXIST);
	if(0 != ret){
		bpf_printk("same iocb_ptr unk");
	}
	return 0;
}

SEC("kprobe/iomap_dio_bio_actor")
int BPF_KPROBE(kpiomap_dio_bio_actor,struct inode *inode, loff_t pos, loff_t length,
struct iomap_dio *dio, struct iomap *iomap)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
	u64 iocb_ptr = (u64)BPF_CORE_READ(dio,iocb);
	struct simple_dio_event* dio_simple = bpf_map_lookup_elem(&event_cache,&iocb_ptr);
	if(dio_simple){
		dio_simple->start_block_io = bpf_ktime_get_ns();
		u64 dio_ptr = (u64)dio;
		u64 dio_uid = dio_simple->start_block_io;
		bpf_printk("do dio: %lld",dio_uid);
		int ret = bpf_map_update_elem(&dio_filter,&dio_ptr,&dio_uid,0);
		return 0;
	}
	return 0;
}

SEC("kprobe/kfree")
int BPF_KPROBE(kpfree,struct iomap_dio *dio)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
	//check if traced file
	u64 iocb_ptr = (u64)BPF_CORE_READ(dio,iocb);
	struct simple_dio_event* dio_simple = bpf_map_lookup_elem(&event_cache,&iocb_ptr);
	if(dio_simple){
		dio_simple->end_block_io = bpf_ktime_get_ns();
		long long dio_size = BPF_CORE_READ(dio,size);
		dio_simple->dio_size = dio_size;
		bpf_get_current_comm(dio_simple->common,16);
		//here change 
		struct dio_info_event* log = bpf_ringbuf_reserve(&ext4_dio_log,sizeof(struct dio_info_event),0);
		if(log){
			__builtin_memcpy(&log->sim_event,dio_simple,sizeof(struct simple_dio_event));
			u64 dio_ptr = (u64)dio;
			u64* dio_uid = bpf_map_lookup_elem(&dio_filter,&dio_ptr);
			if(dio_uid){
				log->dio_uid = *dio_uid;
				bpf_printk("free dio: %lld",*dio_uid);
			}
			bpf_ringbuf_submit(log,0);
			bpf_map_delete_elem(&dio_filter,&dio_ptr);
		}
		bpf_map_delete_elem(&event_cache,&iocb_ptr);
	}
	return 0;
}

// SEC("kprobe/blk_mq_submit_bio")
// int BPF_KPROBE(kpblk_mq_submit_bio,struct bio *bio)
// {
// 	u64 dio_ptr = (unsigned long long)BPF_CORE_READ(bio,bi_private);
// 	bpf_printk("do: dio_ptr: %llu",dio_ptr);
// 	return 0;
// }

SEC("kprobe/__rq_qos_track")
int BPF_KPROBE(kp__rq_qos_track,struct rq_qos *rqos, struct request *rq, struct bio *bio)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
    //check if request is commited at queue_rq
    u64 rq_ptr = (unsigned long long)rq;
    struct rq_submit_event* log = bpf_map_lookup_elem(&rq_cache,&rq_ptr);
    if(log){
        //has_rq means not commit at queue_rq, may merge at sched or fail, now is recyled
        bpf_map_delete_elem(&rq_cache,&rq_ptr);
    }

	//check if belong to target dio
	u64 dio_ptr = (unsigned long long)BPF_CORE_READ(bio,bi_private);
	u64* dio_uid = bpf_map_lookup_elem(&dio_filter,&dio_ptr);
	if(dio_uid){
		//has_ptr != Null, this rq belongs to a select dio, keep going
		struct rq_info_event rq_info;
		__builtin_memset(&rq_info, 0, sizeof(struct rq_info_event));
		rq_info.dio_uid = *dio_uid;
		rq_info.rq_ptr = rq_ptr;
		rq_info.rq_bio_start = bpf_ktime_get_ns();
		//add to rq_cache
		int ret = bpf_map_update_elem(&rq_cache,&rq_ptr,&rq_info,BPF_NOEXIST);
		if(0 != ret){
			bpf_printk("same rq error");
		}
	}
	return 0;
}


SEC("kprobe/nvme_queue_rq")
int BPF_KPROBE(kpnvme_queue_rq,struct blk_mq_hw_ctx *hctx,const struct blk_mq_queue_data *bd)
{
    if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
    u64 key = (unsigned long long)BPF_CORE_READ(bd,rq);
	u64 now = bpf_ktime_get_ns();
    struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
    if(log){
        log->queue_rq = now;
		    //commit hctx_state
		struct hctx_metadata* hctx_state;
		hctx_state = bpf_ringbuf_reserve(&ext4_dio_log,sizeof(*hctx_state),0);
		if(hctx_state){
			hctx_state->hctx_ptr =  (unsigned long long)hctx;
			hctx_state->tp = now;
			hctx_state->queued = BPF_CORE_READ(hctx,queued);
			hctx_state->runed = BPF_CORE_READ(hctx,run);
			hctx_state->numa_node =  BPF_CORE_READ(hctx,numa_node);
			hctx_state->queue_num = BPF_CORE_READ(hctx,queue_num);
			__builtin_memcpy(hctx_state->disk_name,BPF_CORE_READ(bd,rq,rq_disk,disk_name),32);
			bpf_ringbuf_submit(hctx_state,0);
		}
    }
	return 0;
}


SEC("kprobe/scsi_queue_rq")
int BPF_KPROBE(kpscsi_queue_rq,struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
    if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
    u64 key = (unsigned long long)BPF_CORE_READ(bd,rq);
	u64 now = bpf_ktime_get_ns();
    struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
    if(log){
        log->queue_rq = now;
		struct hctx_metadata* hctx_state;
		hctx_state = bpf_ringbuf_reserve(&ext4_dio_log,sizeof(*hctx_state),0);
		if(hctx_state){
			hctx_state->hctx_ptr =  (unsigned long long)hctx;
			hctx_state->tp = now;
			hctx_state->queued = BPF_CORE_READ(hctx,queued);
			hctx_state->runed = BPF_CORE_READ(hctx,run);
			hctx_state->numa_node =  BPF_CORE_READ(hctx,numa_node);
			hctx_state->queue_num = BPF_CORE_READ(hctx,queue_num);
			__builtin_memcpy(hctx_state->disk_name,BPF_CORE_READ(bd,rq,rq_disk,disk_name),32);
			bpf_ringbuf_submit(hctx_state,0);
		}
    }
	return 0;
}

SEC("kprobe/blk_mq_start_request")
int BPF_KPROBE(kpblk_mq_start_request,struct request *rq)
{
	if(using_pid_filter){
		CHECK_TPID(tpid_ext4_dio)
	}
	if(using_common_filter){
		CHECK_COMMON(&tcommon_name[0]);
	}
	u64 key = (unsigned long long)rq;
	struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.rq_start = bpf_ktime_get_ns();
	}
	return 0;
}

SEC("kprobe/scsi_softirq_done")
int BPF_KPROBE(kpscsi_softirq_done,struct request *rq)
{
	u64 key = (unsigned long long)rq;
	struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.scsi_path.softirq_done = bpf_ktime_get_ns();
		log->rq_comp.drive = scsi_drive;
	}
	return 0;
}

SEC("kprobe/scsi_end_request")
int BPF_KPROBE(kpscsi_end_request,struct request *req, blk_status_t error,
		unsigned int bytes)
{
	u64 key = (unsigned long long)req;
	struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.scsi_path.scsi_end = bpf_ktime_get_ns();
	}
	return 0;
}


SEC("kprobe/nvme_pci_complete_rq")
int BPF_KPROBE(kpnvme_pci_complete_rq,struct request *req)
{
	u64 key = (unsigned long long)req;
	struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.nvme_path.pci_complete_rq = bpf_ktime_get_ns();
		log->rq_comp.drive = nvme_drive;
	}
	return 0;
}

SEC("kprobe/nvme_complete_rq")
int BPF_KPROBE(kpnvme_complete_rq,struct request *req)
{
	u64 key = (unsigned long long)req;
	struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.nvme_path.nvme_complete_rq = bpf_ktime_get_ns();
	}
	return 0;
}

SEC("kprobe/blk_account_io_done")
int BPF_KPROBE(kpblk_account_io_done,struct request *req, u64 now)
{
	u64 key = (unsigned long long)req;
	struct rq_info_event* log = bpf_map_lookup_elem(&rq_cache,&key);
	if(log){
		log->rq_comp.account_io = now;
		//write meta data to log
		log->rq_comp.sector = (unsigned long long)BPF_CORE_READ(req,__sector);
		log->rq_comp.hctx_ptr = (unsigned long long)BPF_CORE_READ(req,mq_hctx);
		log->rq_comp.stats_sectors = BPF_CORE_READ(req,stats_sectors);
		log->rq_comp.nr_phys_segments = BPF_CORE_READ(req,nr_phys_segments);
		struct rq_info_event* event = bpf_ringbuf_reserve(&ext4_dio_log,sizeof(struct rq_info_event),0);
		if(event){
			__builtin_memcpy(event,log,sizeof(struct rq_info_event));
			bpf_ringbuf_submit(event,0);
			bpf_map_delete_elem(&rq_cache,&key);
		}
	}
	return 0;
}







