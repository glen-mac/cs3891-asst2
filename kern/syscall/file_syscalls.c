/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <file.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <proc.h>
#include <synch.h>
#include <current.h>
#include <vnode.h>
#include <kern/seek.h>
#include <kern/stat.h>

/*
 * Open system call: open a file.
 */
int 
sys_open(userptr_t filename, int flags, mode_t mode, int *fd_ret)
{
	char fn[PATH_MAX];
	int result;

	/* copy file name from user to kernel space */
	result = copyinstr(filename, fn, PATH_MAX, NULL);
	if (result) {
		return result;
	}

	/* call system function to open file */
	return file_open(fn, flags, mode, fd_ret);
}


/*
 * Write system call: write to a file.
 */
int 
sys_write(int fd, userptr_t buf, size_t nbytes, int *sz)
{
	/* check to see if the file descriptor is sensible */
	if (fd < 0 || fd >= OPEN_MAX) {
	  return EBADF;
	}

	/* call system function to write to a file */
	return file_write(fd, buf, nbytes, sz);
}


/*
 * Read system call: read from a file.
 * this syscall will read buflen bytes from the fd file descriptor into the
 * userland buffer buf and return the number of bytes read in sz.
 */
int 
sys_read(int fd, userptr_t buf, size_t buflen, int *sz)
{
	/* check to see if the file descriptor is sensible */
	if (fd < 0 || fd >= OPEN_MAX) {
	  return EBADF;
	}

	return file_read(fd, buf, buflen, sz);
}

/*
 * sys_dup2
 * this syscall duplicates one file descriptor to another
 */
int
sys_dup2(int oldfd, int newfd, int *fd_ret)
{
	/* check the file descriptors make sense */
	if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
	  return EBADF;
	}
	
	/* assign return value for syscall */
	*fd_ret = newfd;
	
	/* if both fd are the same, return */
	if (oldfd == newfd) {
		return 0;
	}

	/* get exclusive access to oft */
	lock_acquire(of_t->oft_l);

	/* get the old and new oft indices */
	struct fd_table *fd_tab = curproc->fd_t;
	int old_of = fd_tab->fd_entries[oldfd];
	int new_of = fd_tab->fd_entries[newfd];

	/* if we are trying to dup from a closed FD */
	if (old_of == FILE_CLOSED) {
		lock_release(of_t->oft_l);
		return EBADF;
	}

	/* increment reference count */
	struct open_file *of = of_t->openfiles[old_of];
	of->rc = of->rc + 1;

	/* if newfd is currently open, close it */
	if (new_of != FILE_CLOSED) {
		file_close(newfd);
	}

	/* get exclusive access to oft */
	lock_release(of_t->oft_l);

	/* assign new open file table reference to new fd */
	fd_tab->fd_entries[newfd] = fd_tab->fd_entries[oldfd];

	return 0;
}

/* 
 * sys_close
 * closes a file
 */
int
sys_close(int fd)
{
	return file_close(fd);
}


/*
 * sys_lseek
 * shift the offset within an open file object
 */
int
sys_lseek(int fd, off_t pos, int whence, off_t *npos)
{
	int result;

	/* check to see if the file descriptor is sensible */
	if (fd < 0 || fd >= OPEN_MAX) {
	  return EBADF;
	}
	
	/* check to see if whence is a legit value */
	if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
		return EINVAL;
	}

	/* get the file index */
	int of_index = curproc->fd_t->fd_entries[fd];
	
	/* check if fd is closed */
	if (of_index == FILE_CLOSED) {
		return EBADF;
	}

	/* lock the oft so we can read and access it */
	lock_acquire(of_t->oft_l);

	/* get the actual file from the open file table */
	struct open_file *of = of_t->openfiles[of_index];

	/* check if the file is a device */
	if (!VOP_ISSEEKABLE(of->vn)) {
		lock_release(of_t->oft_l);
		return ESPIPE;
	}

	struct stat of_stat;	/* stat of struct to find file size */
	off_t og_pos = of->os;	/* original seek position to restore if needed */

	/* determine new position */
	switch(whence)
	{
	case SEEK_SET:
		of->os = pos;
		break;
	case SEEK_CUR:
		of->os = of->os + pos;
		break;
	case SEEK_END:
		result = VOP_STAT(of->vn, &of_stat);
		if (result) {
			lock_release(of_t->oft_l);
			return result;
		}
		of->os = pos + of_stat.st_size;
		break;
	}

	/* the seek would have been negative */
	if (of->os < 0) {
		of->os = og_pos;
		lock_release(of_t->oft_l);
		return EINVAL;
	}

	/* assign new position */
	*npos = of->os;

	/* release the table */
	lock_release(of_t->oft_l);

	return 0;
}
