# Locked memory limits
程序使用`BPF`的`map`结构来对`record`和一些其他信息进行缓存，这会持续的占据一部分内存空间

程序使用`libbpf`实现`io`事件观测功能，对程序使用内存空间上线设置为`512 MBs`，其原理是通过`sys_call_setrlimit`

```c
#include <sys/resource.h>
    rlimit rlim = {
        .rlim_cur = 512UL << 20, /* 512 MBs */
        .rlim_max = 512UL << 20, /* 512 MBs */
    };

    err = setrlimit(RLIMIT_MEMLOCK, &rlim);
```

可以在`trace_config.json`中配置这一内存限制大小