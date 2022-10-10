#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "ext4_dio_simple.h"
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
	__type(key, unsigned long);
	__type(value,bool);
} file_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024*8);
	__type(key, u64);
	__type(value,struct simple_dio_event);
} event_cache SEC(".maps");

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
		return 0;
	}
	return 0;
}

SEC("kprobe/iomap_dio_complete")
int BPF_KPROBE(kpiomap_dio_complete,struct iomap_dio *dio)
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
		struct simple_dio_event* log = bpf_ringbuf_reserve(&ext4_dio_log,sizeof(struct simple_dio_event),0);
		if(log){
			__builtin_memcpy(log,dio_simple,sizeof(struct simple_dio_event));
			bpf_ringbuf_submit(log,0);
		}
		bpf_map_delete_elem(&event_cache,&iocb_ptr);
	}
	return 0;
}

