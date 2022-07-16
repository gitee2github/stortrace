# Ext4 write
对于ext4
```c
const struct file_operations ext4_file_operations = {
	.llseek		= ext4_llseek,
	.read_iter	= ext4_file_read_iter,
	.write_iter	= ext4_file_write_iter,
	.iopoll		= iomap_dio_iopoll,
	.unlocked_ioctl = ext4_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext4_compat_ioctl,
#endif
	.mmap		= ext4_file_mmap,
	.mmap_supported_flags = MAP_SYNC,
	.open		= ext4_file_open,
	.release	= ext4_release_file,
	.fsync		= ext4_sync_file,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ext4_fallocate,
};
```

5.x版本和3.x版本有较大区别
回溯到3.10的kernel版本，aio的文件操作依然会调用file_op的接口
```c
const struct file_operations ext4_file_operations = {
.read		= do_sync_read,
.write		= do_sync_write,
.aio_read	= generic_file_aio_read,
.aio_write	= ext4_file_write,
}
```
现阶段的ext4所有文件write都走
`ext4_file_write_iter`

```c
static ssize_t
ext4_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	if (unlikely(ext4_forced_shutdown(EXT4_SB(inode->i_sb))))
		return -EIO;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(inode))
		return ext4_dax_write_iter(iocb, from);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext4_dio_write_iter(iocb, from);
	else
		return ext4_buffered_write_iter(iocb, from);
}
```
可以看到此时write的下发有三种路径，和
iocb的ki_flags有关