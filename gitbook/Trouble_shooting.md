# Trouble_shooting

## clang缺失或版本不匹配
修改makefile头文件
```make

```

## BPF_MAP_TYPE_RINGBUF 无法识别
```bash
ringbuf-reserve-submit.bpf.c:11:15: error: use of undeclared identifier 'BPF_MAP_TYPE_RINGBUF'
        __uint(type, BPF_MAP_TYPE_RINGBUF);
                     ^
1 error generated.
```
修改引用文件`#include <linux/bpf.h>`
```c
#include "../libbpf/include/uapi/linux/bpf.h"
```