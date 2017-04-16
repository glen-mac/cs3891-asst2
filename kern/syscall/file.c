#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>


/*
 * file_open
 * deals within opening a file on the kernel side.
 */
int 
file_open(char *filename, int flags, mode_t mode, int *fd) 
{
	int i;

	/* create new file record */
	struct open_file *of = kmalloc(sizeof(struct open_file));
	if (of == NULL) {
		return ENOMEM;
	}

	/* get this thread's file table */
	struct file_table *ft = curthread->t_ft;

	/* create the lock for the file for multi processes */
	struct lock *lk = lock_create("file_lock");
	if (lk == NULL) {
		kfree(of);
		return ENOMEM;
	}

	/* open the vnode */
	struct vnode *vn;
	int fopen_res = vfs_open(filename, flags, mode, &vn);
	if (fopen_res) {
		lock_destroy(lk);
		kfree(of);
	}

	/* make all the assignments to the file */
	of->vn = vn;
	of->fl = lk;
	of->rc = 1;
	of->am = flags & O_ACCMODE;

	/* find the next available file descriptor */
	for (i = 0; i < OPEN_MAX; i++) {
		if (ft->openfiles[i] == NULL) {
			ft->openfiles[i] = of;
			*fd = i;
			return 0;
		}
	}

	/* The process's file table was full, or a process-specific limit on open
	 * files was reached.
	 */
	return EMFILE;
}

/* 
 * file_table_init
 * this function is called when a process is run. The point of it is to
 * clear the file descriptor table within a thread so that searches for
 * free positons may be done when opening files. It also creates the
 * stdin, stdout and stderror file descriptors.
 */
int file_table_init(const char *stdin_path, const char *stdout_path,
		const char *stderr_path)
{
	int i, fd, result;
	char path[PATH_MAX];

	/* if there is no file table for thread - make one! */
	curthread->t_ft = kmalloc(sizeof(struct file_table));
	if (curthread->t_ft == NULL) {
		return ENOMEM;
	}
	
	/* empty the new table */
	for (i = 0; i < OPEN_MAX; i++) {
		curthread->t_ft->openfiles[i] = NULL;
	}

	/* create stdin file desc */
	strcpy(path, stdin_path);
	result = file_open(path, O_RDONLY, 0, &fd);
	if (result) {
		return result;
	}

	/* create stdout file desc */
	strcpy(path, stdout_path);
	result = file_open(path, O_WRONLY, 0, &fd);
	if (result) {
		return result;
	}

	/* create stderror file desc */
	strcpy(path, stderr_path);
	result = file_open(path, O_WRONLY, 0, &fd);
	if (result) {
		return result;
	}

	return 0;
}
