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

/*
 * Open system call: open a file.
 */
int 
sys_open(userptr_t filename, int flags, mode_t mode, int *fd)
{
	char fn[PATH_MAX];
	int result;

	/* copy file name from user to kernel space */
	result = copyinstr(filename, fn, PATH_MAX, NULL);
	if (result) {
		return result;
	}

	/* call system function to open file */
	result = file_open(fn, flags, mode, fd);
	if (result) {
		return result;
	}

	return 0;
}


/*
 * Write system call: write to a file.
 */
int 
sys_write(int fd, userptr_t buf, size_t nbytes, int *sz)
{
	int result;

	/* check to see if the file descriptor is sensible */
	if (fd < 0 || fd >= OPEN_MAX) {
	  return EBADF;
	}

	result = file_write(fd, buf, nbytes, sz);
	if (result) {
		return result;
	}
	
	return 0;
}


/*
 * Read system call: read from a file.
 * this syscall will read buflen bytes from the fd file descriptor into the
 * userland buffer buf and return the number of bytes read in sz.
 */
int 
sys_read(int fd, userptr_t buf, size_t buflen, int *sz)
{
	int result;

	/* check to see if the file descriptor is sensible */
	if (fd < 0 || fd >= OPEN_MAX) {
	  return EBADF;
	}

	result = file_read(fd, buf, buflen, sz);
	if (result) {
		return result;
	}

	return 0;

}


