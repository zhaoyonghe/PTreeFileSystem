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

DEFINE_SRCU(ptreefs_srcu);

static struct vfsmount *ptreefs_mount;
static int ptreefs_mount_count;


static void ptreefs_create_hirearchy(struct super_block *sb,
	struct dentry *root);

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

		if (!__ptreefs_remove(child, parent))
			simple_release_fs(&ptreefs_mount, &ptreefs_mount_count);

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

	if (!__ptreefs_remove(child, parent))
		simple_release_fs(&ptreefs_mount, &ptreefs_mount_count);

	inode_unlock(d_inode(parent));

	synchronize_srcu(&ptreefs_srcu);
}

static int ptreefs_dir_open(struct inode *inode, struct file *file)
{
	// if exists some directories, remove them
	struct dentry *dentry;
	struct list_head *d_subdirs;

	dentry = file_dentry(file);
	d_subdirs = &dentry->d_subdirs;
	if (!(list_empty(d_subdirs))) {
		ptreefs_remove_recursive(list_first_entry(d_subdirs,
			struct dentry, d_child));
	}

	// create new hierarchy
	ptreefs_create_hirearchy(inode->i_sb, inode->i_sb->s_root);

	return dcache_dir_open(inode, file);
}

const struct file_operations ptreefs_dir_operations = {
	.open		= ptreefs_dir_open,
	.release	= dcache_dir_close,
	.llseek     = dcache_dir_lseek,
	.read       = generic_read_dir,
	.iterate    = dcache_readdir,
	.fsync      = noop_fsync,
};

static int ptreefs_fill_super(struct super_block *sb, void *data, int silent)
{
	static const struct tree_descr ptree_files[] = {{""} };
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
	inode->i_mode = S_IFDIR | 0755;
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

static struct dentry *ptree_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, ptreefs_fill_super);
}

static struct file_system_type ptree_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ptreefs",
	.mount		= ptree_mount,
	.kill_sb	= kill_litter_super,
};

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

static struct dentry *start_creating(const char *name, struct dentry *parent)
{
	struct dentry *dentry;
	int error;

	pr_debug("ptreefs: creating file '%s'\n", name);

	if (IS_ERR(parent))
		return parent;

	error = simple_pin_fs(&ptree_fs_type, &ptreefs_mount,
			      &ptreefs_mount_count);
	if (error)
		return ERR_PTR(error);

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent)
		parent = ptreefs_mount->mnt_root;

	inode_lock(d_inode(parent));
	dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(dentry) && d_really_is_positive(dentry)) {
		dput(dentry);
		dentry = ERR_PTR(-EEXIST);
	}

	if (IS_ERR(dentry)) {
		inode_unlock(d_inode(parent));
		simple_release_fs(&ptreefs_mount, &ptreefs_mount_count);
	}

	return dentry;
}

static struct dentry *failed_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	dput(dentry);
	simple_release_fs(&ptreefs_mount, &ptreefs_mount_count);
	return NULL;
}

static struct dentry *end_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	return dentry;
}

static struct inode *ptreefs_get_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_atime = inode->i_mtime =
			inode->i_ctime = current_time(inode);
	}
	return inode;
}

struct dentry *ptreefs_create_dir(const char *name, struct dentry *parent)
{
	struct dentry *dentry = start_creating(name, parent);
	struct inode *inode;

	if (IS_ERR(dentry))
		return NULL;

	inode = ptreefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return failed_creating(dentry);

	inode->i_mode = S_IFDIR | 0755;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(d_inode(dentry->d_parent));
	fsnotify_mkdir(d_inode(dentry->d_parent), dentry);
	return end_creating(dentry);
}



static void replace(char *str)
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


static void ptreefs_create_hirearchy(struct super_block *sb,
						struct dentry *root)
{
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

	while (1) {
		if (can_go_down) {
			pid = task_pid_nr(p);
			get_task_comm(process_name, p);
			replace(process_name);

			sprintf(dir_name, "%d.%s", pid, process_name);

			parent_dir = ptreefs_create_dir(dir_name, parent_dir);
			if (IS_ERR_OR_NULL(parent_dir)) {
				read_unlock(&tasklist_lock);
				return;
			}

			memset(dir_name, 0, 50);
			memset(process_name, 0, 16);
		}

		if (can_go_down && has_children(p)) {
			p = list_first_entry(&p->children,
				struct task_struct, sibling);
		} else if (has_next_sibling(p)) {
			// no children, go to the next sibling
			p = list_first_entry(&p->sibling,
				struct task_struct, sibling);
			can_go_down = true;
			parent_dir = parent_dir->d_parent;
		} else {
			// no chilren, no next sibling
			p = p->real_parent;
			can_go_down = false;
			parent_dir = parent_dir->d_parent;
		}

		if (p == &init_task)
			break;
	}

	read_unlock(&tasklist_lock);
}

static int __init init_ptree_fs(void)
{
	static unsigned long once;

	if (test_and_set_bit(0, &once))
		return 0;

	return register_filesystem(&ptree_fs_type);
}

module_init(init_ptree_fs)
