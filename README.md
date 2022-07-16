# stortrace

## About
ebpf实现的高性能IO追踪和分析工具

High-performance IO tracing and analysis tool based ebpf mechanism

## Documentation
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



