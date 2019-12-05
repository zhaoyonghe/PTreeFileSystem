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


static int ptreefs_create_hirearchy(struct super_block *sb, struct dentry *root);

const struct super_operations ptreefs_super_operations = {
	.statfs         = simple_statfs,
	.drop_inode     = generic_delete_inode,
};

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

// TODO:
static int ptreefs_dir_open(struct inode *inode, struct file *file) {
	// if exists some directories, remove them
	struct dentry *dentry;
	struct list_head *d_subdirs;

	dentry = file_dentry(file);
	d_subdirs = &dentry->d_subdirs;
	if (!(list_empty(d_subdirs))) {
		ptreefs_remove_recursive(list_first_entry(d_subdirs, struct dentry, d_child));
	}

	// create new hierarchy
	ptreefs_create_hirearchy(inode->i_sb, inode->i_sb->s_root);
	return dcache_dir_open(inode, file);
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
	inode->i_mtime = inode->i_atime = 
		inode->i_ctime = current_time(inode);
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

static ssize_t ptreefs_read_file(struct file *file, 
	char __user *buf, size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t ptreefs_write_file(struct file *file, 
	const char __user *buf, size_t count, loff_t *ppos)
{
	return count;
}

const struct file_operations ptreefs_file_operations = {
	.open		= simple_open,
	.read		= ptreefs_read_file,
	.write		= ptreefs_write_file,
};

static struct inode *ptreefs_make_inode(struct super_block *sb, int mode)
{
	struct inode *inode;

	inode = new_inode(sb);

	if (!inode)
		return NULL;

	inode->i_ino = get_next_ino();
	inode->i_mtime = inode->i_atime = 
		inode->i_ctime = current_time(inode);
	inode->i_mode = mode;
	// inode->i_blkbits = PAGE_CACHE_SIZE;
	// inode->i_blocks = 0;

	return inode;
};

struct dentry *ptreefs_create_file(struct super_block *sb,
	struct dentry *dir, const char* name)
{
	struct inode *inode;
	struct dentry *dentry;
	struct qstr qname;

	qname.name = name;
	qname.len = strlen(name);
	// no salt value
	qname.hash = full_name_hash(NULL, name, qname.len);

	dentry = d_alloc(dir, &qname);
	if (!dentry)
		return NULL;

	// permission 0555: read allowed, execute allowed, write prohibiteds
	inode = ptreefs_make_inode(sb, S_IFREG | 0555);
	if (!inode) {
		dput(dentry); // release a dentry
		return NULL;
	}
	inode->i_fop = &ptreefs_file_operations;

	d_add(dentry, inode);

	return dentry;
}

struct dentry *ptreefs_create_dir(struct super_block *sb, 
	const char *name, struct dentry *parent)
{
	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;

	qname.name = name;
	qname.len = strlen(name);
	// no salt value
	qname.hash = full_name_hash(NULL, name, qname.len);

	dentry = d_alloc(parent, &qname);
	if (!dentry)
		return NULL;

	// permission 0555: read allowed, execute allowed, write prohibiteds
	inode = ptreefs_make_inode(sb, S_IFDIR | 0555);
	if (!inode) {
		dput(dentry);
		return NULL;
	}
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;

	d_add(dentry, inode);
	return dentry;
}

static void replace(char* str)
{
	int i;
	int len = strlen(str);
	for (i = 0; i < len; i++) {
		if (str[i] == '/')
			str[i] = '-';
	}
}

static bool has_children(struct task_struct *p)
{
	return !list_empty(&p->children);
}

static bool has_next_sibling(struct task_struct *p)
{
	return p->sibling.next != &p->real_parent->children;
}


static int ptreefs_create_hirearchy(struct super_block *sb, struct dentry *root)
{
	/*
	if (ptreefs_create_dir(sb, "test", root) == NULL) {
		printk("cannot creaet dir!\n");
		return -EINVAL;
	}*/
	struct task_struct *p;
	bool can_go_down = true;
	struct dentry *parent_dir = root;
	pid_t pid;
	char dir_name[50];
	char process_name[16];

	memset(dir_name, 0, 50);
	memset(process_name, 0, 16);
	
	p = &init_task;

	read_lock(&tasklist_lock);

	while(1) {
		if (can_go_down) {
			pid = task_pid_nr(p);
			get_task_comm(process_name, p);
			replace(process_name);

			sprintf(dir_name, "%d.%s", pid, process_name);

			parent_dir = ptreefs_create_dir(sb, dir_name, parent_dir);
			if (parent_dir == NULL)
				return -ENOMEM; // check if it is correct

			memset(dir_name, 0, 50);
			memset(process_name, 0, 16);
		}

		if (can_go_down && has_children(p)) {
			p = list_first_entry(&p->children, struct task_struct, sibling);
		} else if (has_next_sibling(p)) {
			// no children, go to the next sibling
			p = list_first_entry(&p->sibling, struct task_struct, sibling);
			can_go_down = true;
			parent_dir = parent_dir->d_parent;
		} else {
			// no chilren, no next sibling
			p = p->real_parent;
			can_go_down = false;
			parent_dir = parent_dir->d_parent;
		}

		if (p == &init_task) {
			break;
		} 
	}

	read_unlock(&tasklist_lock);
	return 0;
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
	static unsigned long once;

	if (test_and_set_bit(0, &once))
		return 0;

	return register_filesystem(&ptree_fs_type);
}

module_init(init_ptree_fs)