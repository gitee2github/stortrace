write系统调用对应的内核接口如下

```c
asmlinkage long sys_write(unsigned int fd, const char __user *buf, size_t count);
```

在glib中封装为
```c
#include <unistd.h>
ssize_t write(int fd, const void *buf, size_t count);
```

write和pwrite是最基础的对文件写入的系统调用。write会将buf中count个字节写入描述符为fd的文件中，而pread则会将buf中count个字节写入描述符为fd的文件从offset开始的位置中。如果写入成功，这两个系统调用都将返回写入的字节数

pwrite64用于多线程环境，使得write和lseek成为一个原子操作

```c
ssize_t ksys_write(unsigned int fd, const char __user *buf, size_t count)
{
    ......
    loff_t pos, *ppos = file_ppos(f.file);
    if (ppos) {
        pos = *ppos;
        ppos = &pos;
    }
    ret = vfs_write(f.file, buf, count, ppos);
    ......
	return ret;
}

ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf,
		      size_t count, loff_t pos)
{
    ......
    ret = vfs_write(f.file, buf,count, &pos);   
    ......
	return ret;
}
```
可见write/pwrite64都调用了相同的虚拟文件系统函数vfs_write,区别仅在于偏移量的获取方式

因此在系统调用的出入口，事件的观察点为
```shell
tracepoint:syscalls:sys_enter_pwrite64
tracepoint:syscalls:sys_exit_pwrite64


tracepoint:syscalls:sys_enter_write
tracepoint:syscalls:sys_exit_write

```