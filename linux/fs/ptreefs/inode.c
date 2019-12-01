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

const struct super_operations ptreefs_super_operations = {
	.statfs         = simple_statfs,
	.drop_inode     = generic_delete_inode,
};

static int ptreefs_dir_open(struct inode *inode, struct file *file) {
	return 0;
}

const struct file_operations ptreefs_dir_operations = {
	.open 		= ptreefs_dir_open,
	.release 	= dcache_dir_close,
	.llseek     = dcache_dir_lseek,
	.read       = generic_read_dir,
	.iterate    = dcache_readdir,
	.fsync      = noop_fsync,
};

static int ptreefs_fill_super(struct super_block *sb, void *data, int silent)
{
	static const struct tree_descr ptree_files[] = {{""}};
	int err;
	struct inode *inode;

	err  =  simple_fill_super(sb, PTREEFS_MAGIC, ptree_files);
	if (err)
		goto fail;
	sb->s_op = &ptreefs_super_operations;
	//inode = d_inode(sb->s_root);
	inode = new_inode(sb);
	if (!inode)
		goto fail;
	inode->i_ino = 1;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &ptreefs_dir_operations;
	set_nlink(inode, 2);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		pr_err("get root dentry failed\n");
		goto fail;
	}
	return 0;
fail:
	return err;
}

static struct dentry *ptreefs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, ptreefs_fill_super);
}

static struct file_system_type ptree_fs_type = {
	.owner 		= THIS_MODULE,
	.name 		= "ptreefs",
	.mount 		= ptreefs_mount,
	.kill_sb 	= kill_litter_super,
};

// TODO: use this somewhere
// static int ptreefs_root_open(struct inode *inode, struct file *file)
// {
// 	return 0;
// }
// 
// static struct file_operations ptreefs_root_operations = {
// 	.open			= ptreefs_root_open,
// 	.release		= dcache_dir_close,
// 	.llseek			= dcache_dir_lseek,
// 	.read			= generic_read_dir,
// 	.iterate_shared	= dcache_readdir,
// 	.fsync			= noop_fsync,
// };

static ssize_t ptreefs_read_file(struct file *file, char __user *buf,
	size_t count, loff_t *ppos)
{
	return 0;
};

static ssize_t ptreefs_write_file(struct file *file, const char __user *buf,
	size_t count, loff_t *ppos)
{
	return count;
};

const struct file_operations ptreefs_file_operations = {
	.open		= simple_open,
	.read		= ptreefs_read_file,
	.write		= ptreefs_write_file,
};

struct dentry *ptreefs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{
	return NULL;
}

struct dentry *ptreefs_create_dir(const char *name, struct dentry *parent)
{
	return NULL;
}

static int __ptreefs_remove(struct dentry *dentry, struct dentry *parent)
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

void ptreefs_remove_recursive(struct dentry *dentry)
{
	struct dentry *child, *parent;

	if (IS_ERR_OR_NULL(dentry))
		return;

	parent = dentry;
 down:
	inode_lock(d_inode(parent));
 loop:
	/*
	 * The parent->d_subdirs is protected by the d_lock. Outside that
	 * lock, the child can be unlinked and set to be freed which can
	 * use the d_u.d_child as the rcu head and corrupt this list.
	 */
	spin_lock(&parent->d_lock);
	list_for_each_entry(child, &parent->d_subdirs, d_child) {
		if (!simple_positive(child))
			continue;

		/* perhaps simple_empty(child) makes more sense */
		if (!list_empty(&child->d_subdirs)) {
			spin_unlock(&parent->d_lock);
			inode_unlock(d_inode(parent));
			parent = child;
			goto down;
		}

		spin_unlock(&parent->d_lock);

		// TODO: what is ptreefs_mount and ptreefs_mount_count?
		// if (!__ptreefs_remove(child, parent))
		// 	simple_release_fs(&ptreefs_mount, &ptreefs_mount_count);
		__ptreefs_remove(child, parent);

		/*
		 * The parent->d_lock protects agaist child from unlinking
		 * from d_subdirs. When releasing the parent->d_lock we can
		 * no longer trust that the next pointer is valid.
		 * Restart the loop. We'll skip this one with the
		 * simple_positive() check.
		 */
		goto loop;
	}
	spin_unlock(&parent->d_lock);

	inode_unlock(d_inode(parent));
	child = parent;
	parent = parent->d_parent;
	inode_lock(d_inode(parent));

	if (child != dentry)
		/* go up */
		goto loop;

	// TODO: same above
	// if (!__ptreefs_remove(child, parent))
	// 	simple_release_fs(&ptreefs_mount, &ptreefs_mount_count);
	__ptreefs_remove(child, parent);

	inode_unlock(d_inode(parent));

	// TODO: what is ptreefs_scru?
	// synchronize_srcu(&ptreefs_srcu);
}

// TODO: this function is not used.
// choose which one? ptreefs_remove or ptreefs_remove_recursive?
// void ptreefs_remove(struct dentry *dentry)
// {
// 	struct dentry *parent;
// 	int ret;

// 	if (IS_ERR_OR_NULL(dentry))
// 		return;

// 	parent = dentry->d_parent;
// 	inode_lock(d_inode(parent));
// 	ret = __ptreefs_remove(dentry, parent);
// 	inode_unlock(d_inode(parent));
// 	if (!ret)
// 		simple_release_fs(&ptreefs_mount, &ptreefs_mount_count);

// 	synchronize_srcu(&ptreefs_srcu);
// }

static int __init init_ptree_fs(void)
{
	int err;

	err = register_filesystem(&ptree_fs_type);
	// TODO: what is pty_root_table
	// if (!err) {
	// 	register_sysctl_table(pty_root_table);
	// }
	return err;
}

module_init(init_ptree_fs)