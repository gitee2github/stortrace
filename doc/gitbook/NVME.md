# IO在硬件驱动层次的下发流程

## IO request在block层的下发路径
bio在最后的脱离block layer后需要和驱动进行交互，根据硬件设备的不同和调度器的设置方法，会分为两种下发途径

在bio最后的下发阶段，一共有以下几个不同的path
```c
blk_qc_t blk_submit_bio(struct bio* bio)
```
`blk_submit_dio`是所有IO对驱动下发的共同起点
在执行
```c
rq_qos_track(q,rq,bio);
```
之后，准备转化为request的形式，等待最后的提交
```c
blk_mq_bio_to_request(rq,bio,nr_segs)
```
把bio转化为队列所需要的request执行模式，同时会调用
```c
blk_account_io_start(rq)
```
在函数执行的出口统计下发request的数量，`kprobe:blk_account_io_start`也是一个可以追踪的点,代表request的分配成功

如果request的初始化成功，其下发存在以下几种路径

```flow
st=>start:  blk_submit_bio
e=>end
rq_qos_track=>operation: rq_qos_track(q,rq,bio)
bio2rq=>operation: blk_mq_bio_to_request(rq,bio,nr_segs)
rq_count=>operation: blk_account_io_start(rq)
e=>end: insert_request

st->rq_qos_track->bio2rq->rq_count->e
```

### 1 FUA
当IO请求flush_fua的直写。调用`blk_insert_flush(rq)`插入硬件队列`hctx`，之后调用`blk_mq_run_hw_queue`执行下发
这种方式完全跳过了调度器，可能的调用是进行`blk_mq_request_bypass_insert`，但最终还是会调用
`blk_mq_run_hw_queue(data.hctx,true)`来确认下发的过程

对于request,`request->mq_ctx`和`request->mq_hctx`可以找到对应的软件和硬件队列指针，因此也可以知道下发的具体硬件队列

log的格式为
`blk_insert_flush(rq)`
Time rq指针 blk_insert_flush rq对应的hctx指针
`blk_mq_run_hw_queue(data.hctx,rq)`
Time hctx指针 meta-data

```flow
st=>start:  do_fua
op_blk_insert_flush=>operation: blk_insert_flush
op_blk_mq_request_bypass_insert=>operation: blk_mq_request_bypass_insert
e=>end: blk_mq_run_hw_queue(data.hctx,rq)
st->op_blk_insert_flush->op_blk_mq_request_bypass_insert->e
```

### 2 PLUG
plug近似于一种缓存机制，struct blk_plug	*plug;
通过持有短时间内的 I/O request队列，然后可以进行顺序请求的合并，变成一个更大的request，在之后request_queue的执行阶段，会降低自旋锁的争用开销,由于request实质上是预先分配的，因此还可以提高复用分配
```c
struct blk_plug {
	struct request *mq_list; /* blk-mq requests */

	/* if ios_left is > 1, we can batch tag/rq allocations */
	struct request *cached_rq;
	unsigned short nr_ios;

	unsigned short rq_count;

	bool multiple_queues;
	bool has_elevator;
	bool nowait;

	struct list_head cb_list; /* md requires an unplug callback */
};
```
只有`Zone block device`的write操作会跳过plug模式，几乎可以认为是必然开启的，从task_struct，
也就是`current->plug`中可以获取plug的位置

当以下条件任何一个满足则进入`plug`模式
- [1] q->nr_hw_queues == 1
- [2] blk_mq_is_sbitmap_shared(rq->mq_hctx->flags)
- [3] q->mq_ops->commit_rqs 
- [4] !blk_queue_nonrot(q)

此时如果plug中请求的数量过多，则会选择flush plug，调用
```c
blk_flush_plug_list(plug,false)
```
或者就正常调用
```c
static void blk_add_rq_to_plug(struct blk_plug *plug,struct request *rq)
```

这种路径的request下发实际在flush中完成
```c
void blk_fulsh_plug_list(struct blk_plug* plug, bool from_schedule)
```
会递归调用
```c
blk_mq_flush_plug_list(plug,from_schedule)
```
然后多次迭代至plug的list为空，期间的下发函数为
```c
void blk_mq_sched_insert_requests

```
根据内核代码的注释说明，这个函数只会被flush plug的conext调用，在每个cpu上保存了一个usage_counter来记录plug_flush的次数(ctx是per-cpu)
查看是否有设置调度器算法，如果有则调用
```c
>ops.insert_requests(hctx,list,false)
```
没有则对应无调度算法的情况，首先检查硬件队列的状态hctx->dispatch_busy
成功则选择
```c
blk_mq_try_issue_list_directly(hctx,list);
blk_mq_request_issue_directly(rq, list_empty(list));

blk_mq_request_bypass_insert(rq, false, list_empty(list));
blk_mq_end_request(rq, ret);
```
失败则选择
```c
blk_mq_insert_requests(hctx,ctx,list)
```
可能是ctx已经offcpu 这个函数只会被调用在这里，不考虑

最后调用`blk_mq_run_hw_queue(hctx,run_queue_async)`结束下发

### 3 elevator
`elevator`为直接走调度器的接口
```c
blk_mq_sched_insert_requests(rq,false,true,true)
```
实际相当于走了plug模式的后半部分路径

### 4 限制性的plug模式
当`!blk_queue_nomerges(q)`,可以考虑这种特殊的模式，如果bio是可以合并的就进行合并，否则直接下发当前plug中的全部rq，plug list中只保留当前插入的这一个

实际的trace和plug模式没有区别

### 5 直接下发
满足:
[1]q->nr_hw_queues > 1
[2]is_sync
或者
[1]!data.hctx->dispatch_busy
且不进入前面的分支，会选择直接下发的
```c
blk_mq_try_issue_directly(data.hctx,rq,&cookie);
```


## Request在硬件队列hctx的下发执行
虽然request在block层的下发路径有区别，但最终都会转化为执行hctx
```c
blk_mq_run_hw_queue
__blk_mq_delay_run_hw_queue(hctx,async,0)
__blk_mq_run_hw_queue(hctx)
blk_mq_sched_dispatch_requests(hctx)
```
有这样的代码
```c
if(__blk_mq_sched_dispatch_requests(hctx) == -EGAIN){
	if(__blk_mq_sched_dispatch_requests(hctx) == -EGAIN){
		blk_mq_run_hw_queue(hctx,true);
	}
}
```

调度器的设置与否，取决于存储设备的实际设置，也决定了下发的路径
```c
const bool has_sched_dispatch - e && e->type->ops.dispatch_request
if(has_sched_dispatch){
	ret = blk_mq_do_dispatch_sched(hctx);
}else{
	ret = blk_mq_do_dispatch_ctx(hctx);
}
```

### 设备驱动层
## 调度器设置
此处硬件队列hctx的执行已经到了具体的硬件驱动阶段，对于机械硬盘之类的sata接口，
实现方式是SCSI，而对于固态硬盘会采用NVME协议

例如3D-Xpoint和低延迟的NAND设备, 驱动的执行延迟在$\mu s$级别，io的执行数量也在milions的水平(IOPS),(\ref 11 19 26)
因此kernel的软件栈已经成为了主要的性能瓶颈，对于io下发的额外调度反而会造成额外的性能开销，因此NVME可以设置的调用算法默认为`None`

```
cat /sys/block/sda/queue/scheduler
[mq-deadline] kyber bfq none

cat /sys/block/nvme0n1/queue/scheduler
[none] mq-deadline kyber bfq
```

### NVME
#### call NVME driver
对于nvme设备，默认不使用调度算法
```c
cat /sys/block/nvme0n1/queue/scheduler
[none] mq-deadline kyber bfq
```
从`blk_mq_do_dispatch`开始
```c
rq=blk_mq_dequeue_from_ctx(hctx,ctx);
//kprobe:blk_mq_dequeue_from_ctx
```
此处表示rq从ctx中取出，然后选择下次下发的ctx,依据round robin算法
```c
ctx = blk_mq_next_ctx(hctx,rq->mq_ctx);
```
迭代直到`blk_mq_dispatch_rq_list(rq->mq_hctx,&rq_list,1)`返回false

```c
bool blk_mq_dispatch_rq_list()
...
rq = list_first_entry(list, struct request, queuelist);
...
prep = blk_mq_prep_dispatch_rq(rq, !nr_budgets);
...
q->mp_ops->queue_rq(hctx)
...
queued++;
errors++;
blk_mq_end_request(rq, ret);
...
return (queued + errors) != 0;
```
到此处已经调用了NVME的驱动接口

#### NVME exec hctx
暂时不考虑网络方案的NVME，只考虑本地存储的方式，NVME定义的接口位于
```c
static const struct blk_mq_ops nvme_mq_admin_ops = {
	.queue_rq	= nvme_queue_rq,
	.complete	= nvme_pci_complete_rq,
	.init_hctx	= nvme_admin_init_hctx,
	.init_request	= nvme_pci_init_request,
	.timeout	= nvme_timeout,
};

static const struct blk_mq_ops nvme_mq_ops = {
	.queue_rq	= nvme_queue_rq,
	.queue_rqs	= nvme_queue_rqs,
	.complete	= nvme_pci_complete_rq,
	.commit_rqs	= nvme_commit_rqs,
	.init_hctx	= nvme_init_hctx,
	.init_request	= nvme_pci_init_request,
	.map_queues	= nvme_pci_map_queues,
	.timeout	= nvme_timeout,
	.poll		= nvme_poll,
};
```

主要的执行过程为
```c
static blk_status_t nvme_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
spin_lock(&nvmeq->sq_lock);
nvme_sq_copy_cmd(nvmeq, &iod->cmd);
nvme_write_sq_db(nvmeq, bd->last);
spin_unlock(&nvmeq->sq_lock);
```

下发之后的完成使用中断的方式，会调用queue的`complete`接口
```c
static void nvme_pci_complete_rq(struct request *req)
{
	nvme_pci_unmap_rq(req);
	nvme_complete_rq(req);
}

void nvme_complete_rq(struct request *req)
{
	trace_nvme_complete_rq(req);
	nvme_cleanup_cmd(req);

	if (nvme_req(req)->ctrl->kas)
		nvme_req(req)->ctrl->comp_seen = true;

	switch (nvme_decide_disposition(req)) {
	case COMPLETE:
		nvme_end_req(req);
		return;
	case RETRY:
		nvme_retry_req(req);
		return;
	case FAILOVER:
		nvme_failover_req(req);
		return;
	}
}
```



hash<rq_ptr> = log*
 



### SCSI	
### 1 SCSI层概述
### 2 SCSI的调用逻辑




### 1 NVME层概述

### 2 NVME调度
例如3D-Xpoint和低延迟的NAND设备, 驱动的执行延迟在$\mu s$级别，io的执行数量也在milions的水平(IOPS),(\ref 11 19 26)
因此kernel的软件栈已经成为了主要的性能瓶颈，对于io下发的额外调度反而会造成额外的性能开销

NVME采用


对512B的random read IO进行实验，即便不考虑调度算法，kernel软件栈的执行时间也占据了整个IO流程的









## SCSI的调度器下发










sudo cat /sys/kernel/debug/tracing/events/nvme/nvme_sq/format

name: nvme_sq
ID: 1540
format:
	field:unsigned short common_type;	offset:0;	size:2;	signed:0;
	field:unsigned char common_flags;	offset:2;	size:1;	signed:0;
	field:unsigned char common_preempt_count;	offset:3;	size:1;signed:0;
	field:int common_pid;	offset:4;	size:4;	signed:1;

	field:int ctrl_id;	offset:8;	size:4;	signed:1;
	field:char disk[32];	offset:12;	size:32;	signed:1;
	field:int qid;	offset:44;	size:4;	signed:1;
	field:u16 sq_head;	offset:48;	size:2;	signed:0;
	field:u16 sq_tail;	offset:50;	size:2;	signed:0;

print fmt: "nvme%d: %sqid=%d, head=%u, tail=%u", REC->ctrl_id, nvme_trace_disk_name(p, REC->disk), REC->qid, REC->sq_head, REC->sq_tail

static struct elevator_type iosched_bfq_mq = {
	.ops = {
		.limit_depth		= bfq_limit_depth,
		.prepare_request	= bfq_prepare_request,
		.requeue_request        = bfq_finish_requeue_request,
		.finish_request		= bfq_finish_requeue_request,
		.exit_icq		= bfq_exit_icq,
		.insert_requests	= bfq_insert_requests,
		.dispatch_request	= bfq_dispatch_request,
		.next_request		= elv_rb_latter_request,
		.former_request		= elv_rb_former_request,
		.allow_merge		= bfq_allow_bio_merge,
		.bio_merge		= bfq_bio_merge,
		.request_merge		= bfq_request_merge,
		.requests_merged	= bfq_requests_merged,
		.request_merged		= bfq_request_merged,
		.has_work		= bfq_has_work,
		.depth_updated		= bfq_depth_updated,
		.init_hctx		= bfq_init_hctx,
		.init_sched		= bfq_init_queue,
		.exit_sched		= bfq_exit_queue,
	},

	.icq_size =		sizeof(struct bfq_io_cq),
	.icq_align =		__alignof__(struct bfq_io_cq),
	.elevator_attrs =	bfq_attrs,
	.elevator_name =	"bfq",
	.elevator_owner =	THIS_MODULE,
};


struct elevator_type
{
	/* managed by elevator core */
	struct kmem_cache *icq_cache;

	/* fields provided by elevator implementation */
	struct elevator_mq_ops ops;

	size_t icq_size;	/* see iocontext.h */
	size_t icq_align;	/* ditto */
	struct elv_fs_entry *elevator_attrs;
	const char *elevator_name;
	const char *elevator_alias;
	const unsigned int elevator_features;
	struct module *elevator_owner;
#ifdef CONFIG_BLK_DEBUG_FS
	const struct blk_mq_debugfs_attr *queue_debugfs_attrs;
	const struct blk_mq_debugfs_attr *hctx_debugfs_attrs;
#endif

	/* managed by elevator core */
	char icq_cache_name[ELV_NAME_MAX + 6];	/* elvname + "_io_cq" */
	struct list_head list;
};

```c
static struct elevator_type kyber_sched = {
	.ops = {
		.init_sched = kyber_init_sched,
		.exit_sched = kyber_exit_sched,
		.init_hctx = kyber_init_hctx,
		.exit_hctx = kyber_exit_hctx,
		.limit_depth = kyber_limit_depth,
		.bio_merge = kyber_bio_merge,
		.prepare_request = kyber_prepare_request,
		.insert_requests = kyber_insert_requests,
		.finish_request = kyber_finish_request,
		.requeue_request = kyber_finish_request,
		.completed_request = kyber_completed_request,
		.dispatch_request = kyber_dispatch_request,
		.has_work = kyber_has_work,
	}
```
可以看到kyber的调度算法并没有`insert_request`,只有批次插入
kprobe:kyber_insert_requests

cat /sys/block/sdb/nvme/scheduler

```
static void kyber_insert_requests(struct blk_mq_hw_ctx *hctx,
				  struct list_head *rq_list, bool at_head)
		spin_lock(&kcq->lock);
        ...
		blk_mq_sched_request_inserted(rq);
		...
        spin_unlock(&kcq->lock);

```
```
static struct elevator_type iosched_bfq_mq = {
	.ops = {
		.limit_depth		= bfq_limit_depth,
		.prepare_request	= bfq_prepare_request,
		.requeue_request        = bfq_finish_requeue_request,
		.finish_request		= bfq_finish_requeue_request,
		.exit_icq		= bfq_exit_icq,
		.insert_requests	= bfq_insert_requests,
		.dispatch_request	= bfq_dispatch_request,
		.next_request		= elv_rb_latter_request,
		.former_request		= elv_rb_former_request,
		.allow_merge		= bfq_allow_bio_merge,
		.bio_merge		= bfq_bio_merge,
		.request_merge		= bfq_request_merge,
		.requests_merged	= bfq_requests_merged,
		.request_merged		= bfq_request_merged,
		.has_work		= bfq_has_work,
		.depth_updated		= bfq_depth_updated,
		.init_hctx		= bfq_init_hctx,
		.init_sched		= bfq_init_queue,
		.exit_sched		= bfq_exit_queue,
	},

	.icq_size =		sizeof(struct bfq_io_cq),
	.icq_align =		__alignof__(struct bfq_io_cq),
	.elevator_attrs =	bfq_attrs,
	.elevator_name =	"bfq",
	.elevator_owner =	THIS_MODULE,
};
```

```
static void bfq_insert_requests(struct blk_mq_hw_ctx *hctx,
				struct list_head *list, bool at_head)
{
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		bfq_insert_request(hctx, rq, at_head);
		atomic_inc(&hctx->elevator_queued);
	}
}
```

```c
static void bfq_insert_request(struct blk_mq_hw_ctx *hctx, struct request *rq,
			       bool at_head)
blk_mq_sched_request_inserted(rq);
```


```c
static struct elevator_type mq_deadline = {
	.ops = {
		.insert_requests	= dd_insert_requests,
		.dispatch_request	= dd_dispatch_request,
		.prepare_request	= dd_prepare_request,
		.finish_request		= dd_finish_request,
		.next_request		= elv_rb_latter_request,
		.former_request		= elv_rb_former_request,
		.bio_merge		= dd_bio_merge,
		.request_merge		= dd_request_merge,
		.requests_merged	= dd_merged_requests,
		.request_merged		= dd_request_merged,
		.has_work		= dd_has_work,
		.init_sched		= dd_init_queue,
		.exit_sched		= dd_exit_queue,
	},

#ifdef CONFIG_BLK_DEBUG_FS
	.queue_debugfs_attrs = deadline_queue_debugfs_attrs,
#endif
	.elevator_attrs = deadline_attrs,
	.elevator_name = "mq-deadline",
	.elevator_alias = "deadline",
	.elevator_features = ELEVATOR_F_ZBD_SEQ_WRITE,
	.elevator_owner = THIS_MODULE,
};
```
```c
static void dd_insert_requests(struct blk_mq_hw_ctx *hctx,
			       struct list_head *list, bool at_head)
{
	struct request_queue *q = hctx->queue;
	struct deadline_data *dd = q->elevator->elevator_data;

	spin_lock(&dd->lock);
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		dd_insert_request(hctx, rq, at_head);
		atomic_inc(&hctx->elevator_queued);
	}
	spin_unlock(&dd->lock);
}
```