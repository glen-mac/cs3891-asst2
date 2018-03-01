# COMP3891 Extended Operating Systems 2017/S1 - Assignment 2 (System calls and processes) Design Document

> In this assignment you will be implementing a software bridge between a set of file-related system calls inside the OS/161 kernel and their implementation within the VFS (obviously also inside the kernel). Upon completion, your operating system will be able to run a single application at user-level and perform some basic file I/O.

Below you will find a series of questions which were posed during
the design phase of the Assignment 2. Asking these questions before
implementation made for a smooth and simple experience.

## Design Questions

### What primitive operations exist to support the transfer of data to and from kernel space? Do you want to implement more on top of these?

kern/include/copyinout.h outlines the following functions that can be
used to transfer both strings and data from kernel space to userland
and also in reverse.

```
int copyin(const_userptr_t usersrc, void *dest, size_t len);
int copyout(const void *src, userptr_t userdest, size_t len); int
copyinstr(const_userptr_t usersrc, char *dest, size_t len, size_t *got);
int copyoutstr(const char *src, userptr_t userdest, size_t len, size_t
*got);
```

These functions were heavily leveraged throughout the syscall
implementations in order to pass data back and forth with the user
applications. For the syscalls implemented, no additional primitives
were created, as those provided above were sufficient for all uses.

Specific uses of these functions include in open(), in order to pass in
the path to the file that is desired to be open.

### How you will keep track of open files? For which system calls is this useful?

The implementation of file descriptor tracking was the standard unix
solution; this being a per-process array of `OPEN_MAX` ints, in which the
index that is used to index this array is the process file descriptor. The
values stored in this array per index, corresponds to an index in a
global table which is not per-process, and represents a pointer to an
`open_file` struct.

The process structure was modified to include a pointer to a file
descriptor table, of which is initialized when the process is loaded
and run for the first time - and a check is performed to see if the
open file exists yet - and in the event it doesn't then one is created
and initialized.

The `open_file` struct contains important information such as current
offset, access mode, references count to this open file, a pointer to
the underlying vnode that is provided by the VFS layer and a lock to
ensure atomic synchronous handling.

This global open file table is accessible by all processes, with proper
atomic operations which are performed by the open, read, write, close
syscalls. The dup2 syscall affects the per-process file descriptor
table only.

### How will you handle error cases and bad user input?

Througout the implementation of the syscalls, the UNSW os161 man pages
were followed closely to ensure that the spec was followed correctly,
and that upon each inappropriate user input or error, the issue/error
was handled gracefully such that kernel did not panic.

This was also a venture in security, as it is undesirable for user
applications to pass in kernel memory addresses and expect the OS to
treat this as a user provided address.

Specific examples of handling user input properly includes ensuring
that in read(), the file descriptor that the user provides relates to
a legitimate value (not < 0 and > `OPEN_MAX`) and also that the file that
it corresponds to is in fact open.

All potential error cases for all syscalls were covered, and it was found
that additional error types did not have to be created, as the provided
errors were sufficient for all potential user inputs.

The VFS layer provided an excellent platform for handling incorrect user
input, for example - flags used to open files. The underlying VFS layer
provided the error return codes on incorrect interactions with vnodes,
which were passed back to the user through the appropriate means. This
made for less code and a thorough error coverage.

### How will you handle multiple threads performing syscalls at the same time?

The idea of concurrency was taken onboard throughout the design and came
to fruition during implementation. The file descriptor table is accessed
by many syscalls, as outlined above. Currently OS/161 is single threaded
and so there is no need to lock the per process file descriptor table. If
multithreading were to be introduced a lock must be acquired to ensure
that modifications and updates to the table are atomic and do not affect
syscalls currently in action - as very unpredictable outcomes can result.

The global open file table was also equipped with a lock to access,
for the same reason as the per-process file descriptor table - as files
can be closed and affect other syscalls in progress.

A lock was also implemented per open-file to utilise when reading and
shifting forward the pointer to prevent the entire open file table /
file descriptor table being locked - as this would just extend the
critical region and block for longer.

There were no concurrency issues that were come across during
implementation due to the thorough design prior. This played out in
our favour as the time spent debugging was significantly reduced,
and no redesign had to be perform as all syscalls were implemented
with concurrency is mind - which has also helped for the extended
features/syscalls.


# Extended Features

## Design Questions

### Passing arguments from one user program, through the kernel, into
another user program, is a bit of a chore. What form does this take in
C? This is rather tricky, and there are many ways to be led astray. You
will probably find that very detailed pictures and several walk-throughs
will be most helpful.

### How will you determine:

#### (a) the stack pointer initial value;

The initial value of the stack pointer is set within
`as_define_stack()`, which you pass in a pointer `stackptr`. From
this you can then essentially push items on to the stack
(ie. values, strings) and decrement the pointer.

```
result = as_define_stack(as, &stackptr);
if (result) {
      FREE_ALLOCS();
      /* p_addrspace will go away when curproc is destroyed */
      return result;
}
```

#### (b) the initial register contents;

Register contents are abstracted away.

#### (c) the return value;

According to the man page:
```
On success, execv does not return;
instead, the new program begins executing. On failure, execv
returns -1, and sets errno to a suitable error code for the
error condition encountered.
```

Execve does not return if successful, and otherwise returns with
an error code, which is then passed up and is stored in errno.

#### (d) whether you can exec the program at all?

Defensive programming checks are made, such as string length
of program name, where the `userptr_t` for the program name is
pointing to is not NULL and not in kernel space. Other than that
checking if a file is actually on disk requires checking `stat`,
which is unimplemeneted. Much like calling `open` on a file,
error checking if the file actually exists is left to the lower
levels. For example, `load_elf` will return an error if it can't
load a file, and the same with `vfs_open`.


### What new data structures will you need to manage multiple processes?

We implemented a pid management system, consisting of an array of pid
objects which are associated strongly with the `wait_pid` and the exit
syscalls.

Each pid object has the following struct:

```
struct proc_pid {
  pid_t pid_id;           /* process pid ID */
  pid_t ppid_id;          /* process' parent pid ID */
  int pid_estatus;        /* process exit status */
  int pid_exited;         /* has process exited? */
  struct cv *pid_cv;      /* pid condition variable */
};
```

As can be seen, the process pid is stored, along with the process pid of
the parent, the exist status, a flag to show if the process has exited yet
and finally a condition variable used for waiting on the process exiting.

### What relationships do these new structures have with the rest of the system?

This data structure in particular is used to manage the waitpid and
the exit system calls, along with the getpid and fork test. Alot of
the advanced functionality relies on a structure pid management system,
and we believe the system implemented is robust and works for all the
syscalls that were implemented.

The table is created at system boot, and destroyed on system shudown.

### How will you manage file accesses? When invoke the cat command, and it starts to read file1, what will happen if the shell also tries to read file1? What would you like to happen?

File accesses are managed such that multiple can be called at the same
time, and all will be handled atomically.

When both functions are called to read the file - it has already been set
up such that each process calling read on the file had already opened
up a separate file in the open file table with different offsets, such
that when one process reads the file atomically, it does not affect the
offset of the other open file table entry when that process goes to read
the same file (again, atomically).

### How you will keep track of running processes. For which system calls is this useful?

Running process are kept track using the pid management system outlined
above. This system is useful for the waitpid, exit, getpid, fork and
execve system calls.

### How you will implement the execv system call. How is the argument passing in this function different from that of other system calls?

Execv has the base of `runprogram()` with the difference being that
`runprogram()` does not pass any argument vectors tto the newly created
process. The difference between execve and other systems calls (including
runprogram) is that the arguments being passed in are an array of strings.

Implementing `execv` requires a number of steps:

1. Error checking / defensive programming. Including: checking addresses of pointers as practically possible, to determine that they're not null and not pointing to kernel space
2. Copy in arguments from user space to kernel space (both the program name, and the argument vectors to pass to it) 
3. Make sure the total length of all the arguments is within the limit of `ARG_MAX` 
4. Keep pointers to each of the argument vectors in kernel space 
5. Create a new address space, switch to it, and destroy the old address space 
6. Once this is done the stack pointer is available. Copy the argument vectors on to the stack, and keep track of the addresses in user space of where they are stored 
7. Push the user space addresses of the arguments on to the stack 8. Switch to the new process, giving the current stack pointer and the pointer to the array of argument vector pointers

### How you will implement the fork system call?

The following is the outline of the fork() algorithm:

1. Duplicate the trapframe of the parent 
2. Duplicate the address space of the parent 
3. Create a new process using `proc_create_runprogram()`
4. Duplicate the file descriptor table, and update the refcounts to all the open file table entries that the fdt of the parent process has open
5. Assign the duplicated address space to the new process 
6. Thread fork the parent process using `thread_fork()` and pass the entry point as the `child_execute()` function we wrote.  
7. Return the value of the pid of the child process

The algorithm of the `child_execute` function is as follows:

1. Set the v0 register to 0, as it is the child process (child process returns 0 from a `fork()`) 
2. Run `enter_forked_process` and pass in the child's trapframe

The algorithm of the `enter_forked_process()` function is as follows:

1. Increment the epc register by 4 to prevent an infinite loop
2. Signify no error by putting 0 in a3 register 
3. Warp to userland with `mips_userland(trapframe)`


### How you will implement the exit system call?

The exit syscall works by updated the metadata of the pid object for the
current process, followed by broadcasting to all processes listening on
the condition variable of the pid that the process is exiting.

The process then properly terminates by doing the following:

1. Remove the current thread from the current process 
2. Destroy the current process 
3. Kill the current thread (will be exorcised on thread switch)

### How you will implement the waitpid system call?

This syscall works by simply waiting on the broadcast signal sent by
the exiting process, sleeping otherwise.  Once this has been signaled,
the process is then nulled and removed from the pid management system.

