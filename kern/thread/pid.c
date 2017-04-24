/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2010
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

/*
 * Core kernel-level pid management system.
 */

#include <types.h>
#include <kern/errno.h>
#include <limits.h>
#include <lib.h>
#include <array.h>
#include <cpu.h>
#include <spl.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <threadlist.h>
#include <threadprivate.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <addrspace.h>
#include <pid.h>


/* global data for controlling pid table */
static struct proc_pid **pid_table;  /* pid table */
static int pid_alloc;                          /* number of allocated pids */
static struct lock *pt_lock;                  /* lock for pid table items */
static int pid_top;                          /* next pid to continue from */
/* so if the ppid_id is PID_INVALID and pid_exited is true then the pid
 * structure is a zombie one and can be free'd - we must have been waiting
 * on it
 */

void
pidtable_init()
{
  /* create the pid table */
  pid_table = kmalloc(sizeof(struct proc_pid) * (PID_MAX+1));
  pt_lock = lock_create("pid stat lock");
  pid_alloc = 0;
  pid_top = PID_MIN;
}


void
pidtable_destroy()
{
  /* destroy the pid table */
  kfree(pid_table);
  lock_destroy(pt_lock);
}

/*
 * pid_create()
 * creates a pid structure, probably because we are launching a new process.
 * returns the new pid id
 */
pid_t
pid_create(pid_t ppid)
{
  lock_acquire(pt_lock);

  /* check if we can allocate */
  pid_t pid = pid_next();
  if (pid == -1) {
    return -1;
  }
  
  /* make the pid struct */
  struct proc_pid *pp = kmalloc(sizeof(struct proc_pid));
  
  pp->ppid_id = ppid;             /* set the parent pid */
  pp->pid_id = pid;
  pp->pid_estatus = 0;
  pp->pid_exited = 0;
  pp->pid_cv = cv_create("pid cv");

  pid_table[pid] = pp;

  pid_alloc++;

  lock_release(pt_lock);

  return pid;
}

/*
 * pid_next()
 * gets the next available pid in the pid table. assumes a lock is held on
 * the the pid table upon calling this function
 */
pid_t
pid_next()
{
  int i, retval = -1;
  for (i = pid_top; i <= PID_MAX; i++)
  {
    /* if slot is empty or has a pid struct that is ready to be destroyed */
    if (
        pid_table[i] == NULL || 
        (pid_table[i]->ppid_id == (pid_t)PID_INVALID &&
         pid_table[i]->pid_exited)
        )
    {
      pid_destroy(pid_table[i]);
      retval = i;
      pid_top = i+1;
      break;
    }
  }
  /* loop back to the start */
  if (pid_top > PID_MAX) {
    pid_top = PID_MIN;
  }
  return retval;
}

/*
 * pid_destroy()
 * destroys a pid structure, probably on finding a residual process
 */
void
pid_destroy(struct proc_pid *pp)
{
  /* sanity check */
  if (pp != NULL)
  {
    
    /* null the entry and free all alocations */
    pid_table[pp->pid_id] = NULL;
    cv_destroy(pp->pid_cv);
    kfree(pp);

  }
}

pid_t
pid_wait(pid_t pid, pid_t ppid, int *exit_status)
{

  lock_acquire(pt_lock);

  struct proc_pid *pp = pid_table[pid];
  if (pp == NULL) {
    lock_release(pt_lock);
    return ESRCH;
  } 

  /* not our child, so go away */
  if (pp->ppid_id != ppid) {
    lock_release(pt_lock);
    return ECHILD;
  }

  /* wait for pid to exit() */
  if (!pp->pid_exited) {
    cv_wait(pp->pid_cv, pt_lock);
  }

  /* deem the pid invalid to be cleaned up */
  pp->ppid_id = PID_INVALID;

  /* return the exit status of the pid */
  *exit_status = pp->pid_estatus;

  lock_release(pt_lock);

  return 0;
}

/*
 * pid_exit()
 * called when a process ends, probably to clean it up and help for a process
 * that is waiting on it to exit
 */
void
pid_exit(pid_t pid, int exit_status)
{
  lock_acquire(pt_lock);
  
  pid_alloc--;

  /* find current pid struct we are exiting */
  struct proc_pid *pp = pid_table[pid];

  /* set struct exit info */
  pp->pid_exited = 1;
  pp->pid_estatus = exit_status;

  /* broadcast to all procs waiting on this proc ending that we are done */
  cv_broadcast(pp->pid_cv, pt_lock);

  lock_release(pt_lock);
}

