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
 * file_read
 * system level function from reading from a vnode.
 * The idea is we want to get the current thread, read from it's FDT and get
 * the pointer to the vnode we want, and then we want to read buflen bytes
 * at maximum from the vnode and we want to read into buf.
 */
int
file_read(int fd, userptr_t buf, size_t buflen, int *sz)
{
	int result;
	struct open_file *of	= curthread->t_ft->openfiles[fd];
	struct vnode *vn		= of->vn;

	struct iovec	iovec_tmp;
	struct uio		uio_tmp;

	unsigned char	buf_tmp[buflen];
	
	/* initialize a uio with the read flag set, pointing into our buffer */
	uio_kinit(&iovec_tmp, &uio_tmp, &buf_tmp, buflen, of->os, UIO_READ);

	/* read from vnode into our uio object */
	result = VOP_READ(vn, &uio_tmp);
	if (result) {
		return result;
	}

	*sz = buflen;

	/* copy data out to user space */
	copyout(&buf_tmp,  buf, buflen);

	return 0;
}

/*
 * Write system call: write to a file.
 */
int 
file_write(int fd, userptr_t buf, size_t nbytes, int *sz)
{
	int result;
	struct open_file *of	= curthread->t_ft->openfiles[fd];
	struct vnode *vn		= of->vn;

	struct iovec	iovec_tmp;
	struct uio		uio_tmp;

	/* acquire lock on the file */
	lock_acquire(of->fl);

	/* initialize a uio with the write flag set, pointing into our buffer */
	uio_uinit(&iovec_tmp, &uio_tmp, buf, nbytes, of->os, UIO_WRITE);

	/* write into vnode from our uio object */
	result = VOP_WRITE(vn, &uio_tmp);
	if (result) {
		lock_release(of->fl);
		return result;
	}

	/* find the number of bytes written */
	*sz = uio_tmp.uio_offset - of->os;

	/* update the seek pointer in the open file */
	of->os = uio_tmp.uio_offset;

	/* release the lock on the file */
	lock_release(of->fl);
  
  return 0;
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
