# Getting_started

## Introduction
`stortrace`是基于`libbpf`实现的对特定`io`事件的追踪功能，和一般的例如`biosnoop,ext4snoop`等`bcc`的io检测工具相比，实现了对于一次IO_submit过程在系统调用，虚拟文件系统，物理文件系统，块设备等多个不同阶段的时延汇总记录，并提供一些更完善的可视化记录

## Contents of this chapter
本章内容为使用`stortrace`之前需要完成的工作，包括:
- [1]`stortrace`的软件依赖
- [2]`stortrace`安装
- [3]`config`的设置方法
- [4]`Trouble_shooting`安装过程中的常见问题
