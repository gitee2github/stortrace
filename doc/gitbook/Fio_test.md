# 基准测试
## Hardware
### HDD

### NVME

### FLASH

## File_system
```c
sudo dd if=/dev/zero of=fat32.image count=8192000
```
### Ext4
```c
sudo dd if=/dev/zero of=ext4.image count=8192000
sudo mkfs.ext4 ext4.image
mkdir ext4
sudo mount -t ext4 ext4.image ext4/
```

### XFS
> sudo apt-get install xfsprogs

```c
sudo dd if=/dev/zero of=xfs.image count=8192000
sudo mkfs.xfs xfs.image 
mkdir xfs
sudo mount -t xfs xfs.image xfs/
```
### FAT32
mkfs.fat 4.2 (2021-01-31)
```c
sudo dd if=/dev/zero of=fat32.image count=8192000
sudo mkfs.vfat fat32.image
mkdir fat32
sudo mount -t vfat fat32.image fat32/
```

查看挂载是否成功
```shell
df -T -h
```

检测测试文件系统
```c
kprobe:xfs_file_write_iter//xfs
kprobe:fat_write_begin//fat32
kprobe:ext4_file_write_iter//ext4
```

以xfs为例，启动挂载点
```bpf
bpftrace -e 'tracepoint:scsi:scsi_dispatch_cmd_start /pid == 45006/{printf("exec by %s\n",comm);}'
```
测试一个写入
```
cd xfs
touch test
chmod 777 test
echo 123 >test
```
显示调用进程`echo`调用了`xfs_file_write_iter`
```
Attaching 1 probe...
exec by echo
```
## FIO
### Install Fio
Ubuntu jammy
> sudo apt-get -y install fio


OpenEuler

>sudo yum install xfsprogs

###

fio -name=xfs_test -filename=/dev/loop16 -direct=1 -iodepth=20 -thread -rw=randread -ioengine=sync -bs=4k -size=1G -numjobs=8 -runtime=30 -group_reporting



### D

### 
为了找到什么函数启动调用了write 需要根据中断去查找
sys_write:
    0xffffffffa3d76171 -> __x64_sys_write
    0xffffffffa480007c -> entry_SYSCALL_64_after_hwframe

write是一种调用的软中断 因此需要找irq的中断

cat /sys/kernel/debug/tracing/events/irq/softirq_entry/format
tracepoint:irq:softirq_entry

name: softirq_entry
ID: 149
format:
        field:unsigned short common_type;       offset:0;       size:2; signed:0;
        field:unsigned char common_flags;       offset:2;       size:1; signed:0;
        field:unsigned char common_preempt_count;       offset:3;       size:1; signed:0;
        field:int common_pid;   offset:4;       size:4; signed:1;

        field:unsigned int vec; offset:8;       size:4; signed:0;

print fmt: "vec=%u [action=%s]", REC->vec, __print_symbolic(REC->vec, { 0, "HI" }, { 1, "TIMER" }, { 2, "NET_TX" }, { 3, "NET_RX" }, { 4, "BLOCK" }, { 5, "IRQ_POLL" }, { 6, "TASKLET" }, { 7, "SCHED" }, { 8, "HRTIMER" }, { 9, "RCU" })


irqaction 每个中断信息的动作描述符号
设备信息
/**
 * struct irqaction - per interrupt action descriptor
 * @handler:	interrupt handler function
 * @name:	name of the device
 * @dev_id:	cookie to identify the device
 * @percpu_dev_id:	cookie to identify the device
 * @next:	pointer to the next irqaction for shared interrupts
 * @irq:	interrupt number
 * @flags:	flags (see IRQF_* above)
 * @thread_fn:	interrupt handler function for threaded interrupts
 * @thread:	thread pointer for threaded interrupts
 * @secondary:	pointer to secondary irqaction (force threading)
 * @thread_flags:	flags related to @thread
 * @thread_mask:	bitmask for keeping track of @thread activity
 * @dir:	pointer to the proc/irq/NN/name entry
 */
struct irqaction {
	irq_handler_t		handler;
	void			*dev_id;
	void __percpu		*percpu_dev_id;
	struct irqaction	*next;
	irq_handler_t		thread_fn;
	struct task_struct	*thread;
	struct irqaction	*secondary;
	unsigned int		irq;
	unsigned int		flags;
	unsigned long		thread_flags;
	unsigned long		thread_mask;
	const char		*name;
	struct proc_dir_entry	*dir;
} ____cacheline_internodealigned_in_smp;
