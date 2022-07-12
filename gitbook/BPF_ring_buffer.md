# BPF_ring_buffer
环形缓冲区`bpf_ring_buffer`是一种新的数据结构，解决了BPF perf缓冲区，也就是当前bpf程序从kernel把数据发送到用户空间的方法的一些缺陷，主要是内存使用率的问题，和输入map的事件重新排序的问题

`ring_buffer`满足对于`perf_buffer`的兼容性，也提供了新的`reserve/commit`接口


## BPF ringbuf vs BPF perfbuf
当bpf程序收集到需要发送到用户空间的数据，一般选择使用`perfbuf`缓冲区，这种缓冲是对于`per_cpu`单独定义，存在两个主要的缺点，分别是内存使用效率低和cpu之间同步导致的事件重排

为了解决这些问题，Linux5.8之后的bpf支持了一种新的数据结构`bpf_ringbuff`，环形缓冲区是一个多生产者单消费者`(MPSC)`队列,可以在多个cpu之间共享

`bpf_ring_buff`保持了之前的主要特性
- [1]可变长度的数据记录
- [2]通过内存映射实现从`user_space`的数据读取，无需额外的内存复制和系统调用
- [3]对两个`epoll_notification`都支持
- [4]最小绝对延迟的`busy_loop`

在以下问题上做了优化
- [1]内存负载
- [2]数据排序
- [3]额外的内存拷贝开销
  
## 内存负载(Memory overhead)
`bpf_perfbuf`是`per_cpu`的机制，因此在[限制内存](gitbook/../Locked_memory_limits.md)提到了`libbpf`需要手动设置内存资源的上限，但依然需要为每个cpu都分配足够大小的缓冲区，这需要对事件的周期性和波动性做出足够程度的预留，因此要么因为过度安全而造成了资源浪费，要么就需要承担数据丢失

`BPF_ringbuf`在所有cpu之间共享，允许使用一个大的公共缓冲区来处理这个问题,与`BPF perfbuf`相比，更大的缓冲区可以更好的处理波动性，也可以降低`RAM`开销

比如当cpu数量拓展，`bpf_ringbuff`就不一定需要线性的增加分配

## 数据排序(Event ordering)

如果`BPF`应用程序必须跟踪相关事件，例如，进程启动和退出、网络连接生命周期事件,io下发等，事件的正确排序就很重要
对于`BPF_perfbuf`，如果相关事件在不同的 `CPU`上快速连续发生，它们可能会乱序传递,这又是由于`BPF_perfbuf`的`per-CPU`性质

例如对于进程的`fork/exec/exit`进行追踪，但是`fork/exec/exit`经常会发生跨cpu的切换，而且是非常快速的连续调用，如果正常的把记录发送到`bpf_perfbuf`，就需要重新建立逻辑映射关系，使观测问题变得复杂

得益于`ringbuff`的新特性，事件共享同一个缓冲区，这将简化处理逻辑

## 额外的内存拷贝开销(Wasted work and extra data copying)
基于`bpf_perfbuf`的使用方式，bpf程序必须先准备一个数据样本，然后复制到`perbuf`缓冲区，再发送到用户空间，这个过程进行了两次数据复制

而基于`bpf_ringbuf`，使用`reservatiobn\submit`，可以通过`reserve`进行空间的预留，然后bpf程序可以直接对data sample进行传输

## Performance and applicability
虽然`bpf_perfbuf`由于cpu之间的隔离性理论上具有更好的并发性能，但这个高吞吐量仅具有理论意义

https://patchwork.ozlabs.org/project/netdev/patch/20200529075424.3139988-5-andriin@fb.com/

根据实际的基准测试，`ring_buff`在任何情况都是更好的，这和代码实现的事件分发路径有关，`bpf_ring_buff`始终是更好的上位替代

仅有的特殊情况是，当bpf程序必须从`NMI(non-maskable interrupt)`这样的不可屏蔽中断的上下文运行，例如检测`cpu-cycles`，由于BPF的`ring_buff`内部使用了一个轻量的`spin-lock`,即便`ring_buff`本身依然有空间，但仍然会因为高争用导致数据丢失













