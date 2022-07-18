# Libbpf_log
如果需要观测`stortrace`程序自身的输出，则可以调整日志输出的等级

`libbpf`中设置有`WARN,INFO,DEBUG`三种等级
```c
enum libbpf_print_level {
        LIBBPF_WARN,
        LIBBPF_INFO,
        LIBBPF_DEBUG,
};
```
实现不同等级的`log print`是通过注册回调函数

```c
int print_libbpf_log(enum libbpf_print_level lvl, const char *fmt, va_list args)
{
    if (!FLAGS_bpf_libbpf_debug && lvl >= LIBBPF_DEBUG)
        return 0;
    return vfprintf(stderr, fmt, args);
}
libbpf_set_print(print_libbpf_log);
```

config中可以提供设置
