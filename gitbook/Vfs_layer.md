# VFS
## VFS层的io下发
### vfs_write
```c
ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    ......
    ret = rw_verify_area(WRITE, file, pos, count);
    ......
	file_start_write(file);
	if (file->f_op->write)
		ret = file->f_op->write(file, buf, count, pos);
	else if (file->f_op->write_iter)
		ret = new_sync_write(file, buf, count, pos);
	else
		ret = -EINVAL;
	if (ret > 0) {
		fsnotify_modify(file);
		add_wchar(current, ret);
	}
	inc_syscw(current);
	file_end_write(file);
	return ret;
}
```
`rw_verify_area()`是检查pos和count的位置是否可以写入，例如持有锁的情况

如果底层的文件系统制定了file_opeeration
的write函数指针则调用
> file->f_op->write
> 
否则则使用VFS的通用写入函数
> new_sync_write

因此，若此时
```c
struct file_operations {
    ...
	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
    ...
}
```
若使用file_operations中的write函数，则当前io已经下发到实际的文件系统层