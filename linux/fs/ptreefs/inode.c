#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/magic.h>
#include <linux/idr.h>
#include <linux/parser.h>
#include <linux/fsnotify.h>
#include <linux/seq_file.h>

static int ptreefs_fill_super(struct super_block *s, void *data, int silent)
{

}

struct dentry *debugfs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{

}

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent)
{

}

void debugfs_remove_recursive(struct dentry *dentry)
{
	
}

static int __debugfs_remove(struct dentry *dentry, struct dentry *parent)
{
	int ret = 0;

	if (simple_positive(dentry)) {
		dget(dentry);
		if (d_is_dir(dentry))
			ret = simple_rmdir(d_inode(parent), dentry);
		else
			simple_unlink(d_inode(parent), dentry);
		if (!ret)
			d_delete(dentry);
		dput(dentry);
	}
	return ret;
}

void debugfs_remove(struct dentry *dentry)
{
	struct dentry *parent;
	int ret;

	if (IS_ERR_OR_NULL(dentry))
		return;

	parent = dentry->d_parent;
	inode_lock(d_inode(parent));
	ret = __debugfs_remove(dentry, parent);
	inode_unlock(d_inode(parent));
}


static struct dentry *devpts_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, ptreefs_fill_super);
}

static int __init init_ptree_fs(void)
{
	int err = register_filesystem(&ptree_fs_type);
	if (!err) {
		register_sysctl_table(pty_root_table);
	}
	return err;
}
module_init(init_ptree_fs)