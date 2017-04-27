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
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
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
#include <proc.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <endian.h>
#include <copyinout.h>
#include <lib.h>
#include <syscall.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <proc.h>
#include <pid.h>
#include <kern/wait.h>

static void child_execute(void *tf, long unsigned int pid);

/*
 * child_execute
 * the entry point for a child proc upon process fork
 */
static void
child_execute(void *tf, long unsigned int pid)
{
	struct trapframe tf_new;
	tf_new = *(struct trapframe *) tf;

	pid_t pid_new = (pid_t) pid;

	/* set the return value to 0 (child proc) */
	tf_new.tf_v0 = pid_new;

	/* run enter func from syscall.c */
	enter_forked_process(&tf_new);

	/* child_execute does not return. */
	panic("child_execute returned\n");

}

/*
 * fork system call: fork process execution.
 */
int
sys_fork(struct trapframe *tf, pid_t * pid)
{
	int result;

	/* create trapframe for the child */
	struct trapframe *tf_child = kmalloc(sizeof(struct trapframe));
	if (tf_child == NULL) {
		return ENOMEM;
	}

	/* copy the parent trapframe struct data into the child */
	*tf_child = *tf;

	/* create the address space for the child */
	struct addrspace *as_child = NULL;
	as_copy(curproc->p_addrspace, &as_child);
	if (as_child == NULL) {
		return ENOMEM;
	}

	/* create the new proc */
	struct proc *new_proc = proc_create_runprogram(curproc->p_name);
	if (new_proc == NULL) {
		return ENOMEM;
	}

	/* create the file descriptor table as a copy of the parent's */
	new_proc->fd_t = kmalloc(sizeof(struct fd_table));
	*(new_proc->fd_t) = *curproc->fd_t;

	/* assign the new addresspace to the child proc */
	new_proc->p_addrspace = as_child;

	/* fork thread, giving entry point */
	result = thread_fork("new forked process", new_proc, &child_execute,
			     tf_child, 0);
	if (result) {
		return result;
	}

	/* return current pid as the parent */
	*pid = new_proc->p_pid;

	return 0;
}

/*
 * sys_getpid
 * return the pid of the current process
 */
int
sys_getpid(pid_t * pid)
{
	*pid = curproc->p_pid;
	return 0;
}

/*
 * sys__exit
 * exit the current process, closing it and its threads down safely
 */
int
sys__exit(int exit_status)
{
	pid_exit(curproc->p_pid, exit_status);
	panic("sys_exit: unexpected return from thread_exit() \n");
}

/*
 * sys_waitpid
 * wait for a process to exit, which has to be a child process of the 
 * calling process. may save the exit status
 */
int
sys_waitpid(pid_t pid, userptr_t status, int options, int *retPid)
{
	int result;

	/* sanity checks for pid number */
	if (pid < PID_MIN || pid > PID_MAX) {
		return ESRCH;
	}

	/* ensure flags are legit */
	if (options != 0 && options != WUNTRACED && options != WNOHANG) {
		return EINVAL;
	}

	(void) options;		/* pretend we use options */
	int estatus;		/* status temp variable to write back to userland */
	*retPid = (int) pid;	/* pid variable to pass back to user (set first) */


	/* wait on process to end */
	result = pid_wait(pid, curproc->p_pid, &estatus);

	/* if there was an error, return -1 */
	if (result) {
		*retPid = -1;
		return result;
	}

	/* check if status addr is null */
	if (status != NULL) {
		result = copyout(&estatus, status, sizeof(estatus));
		if (result) {
			*retPid = -1;
			return result;
		}
	}

	return result;
}
