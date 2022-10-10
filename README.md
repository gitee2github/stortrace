# stortrace

## About
ebpf实现的高性能IO追踪和分析工具

High-performance IO tracing and analysis tool based ebpf mechanism

## Documentation
<<<<<<< HEAD
`stortrace`是基于`libbpf`实现的对特定`io`事件的追踪功能，和一般的例如`biosnoop,ext4snoop`等`bcc`的io检测工具相比，实现了对于一次IO_submit过程在系统调用，虚拟文件系统，物理文件系统，块设备等多个不同阶段的时延汇总记录，并提供一些更完善的可视化记录

Wiki包含了stortrace的实现细节和使用方式
* [Stortrace_wiki](doc/gitbook/SUMMARY.md)


使用gitbook的方式组织，支持使用web方式查看stortrace的文档
```bash
cd doc
gitbook init
gitbook serve
```

## Quick start tutorial
### I/O latency record

### Latency visualization

### Select I/O events

### Workload characteristic analysis


## Licensing
## Related Resouces



=======
`stortrace`是基于`libbpf`实现的对特定`io`事件的追踪功能，和一般的例如`biosnoop,ext4snoop`等`bcc`的io检测工具相比，实现了对于一次IO_submit过程在系统调用，文件系统，块设备等多个不同阶段的时延汇总记录，并提供一些易于观测的可视化结果

`stortrace`的主要目标为监测数据写入disk的过程,常见程序的主动落盘例如`Mysql`会选择
`direct_io+sync write`,例如`redis`写入`aof`进行持久化或者`leveldb`写入`sst_table`会使用`fsync`

`stortrace`支持对上述场景进行追踪和分析，实现了对于`direct_io`和`fsync`的`trace`功能，观察数据写入disk的情况

`mysql`
[mysql_trace](https://gitee.com/fangwater/stortrace/blob/dev/doc/gitbook/mysql.md)
`redis`
`leveldb`
## Overview
### I/O latency record

### Latency visualization

### Select I/O events

### Workload characteristic analysis

## Licensing
## Related Resouces



>>>>>>> 6cd684f (finish dio and blk_trace,still bug)
