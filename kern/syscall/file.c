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
#include <proc.h>


/*
 * file_open
 * deals within opening a file on the kernel side.
 */
int 
file_open(char *filename, int flags, mode_t mode, int *fd_ret) 
{
        int i, result, fd = -1, of = -1;
        struct vnode *vn;

        /* open the vnode */
        result = vfs_open(filename, flags, mode, &vn);
        if (result) {
                return result;
        }

        /* get this process' file descriptor table */
        struct fd_table *fd_t = curproc->fd_t;

        /* lock the file desc table for the proc */
        lock_acquire(fd_t->fdt_l);
  
        /* lock the open file table for the proc */
        lock_acquire(of_t->oft_l);

        /* find the next available file descriptor in process table */
        for (i = 0; i < OPEN_MAX; i++) {
                if (fd_t->fd_entries[i] == FILE_CLOSED) {
                        fd = i;
                        break;
                }
        }

        /* find the next available spot in global open file table */
        for (i = 0; i < OPEN_MAX; i++) {
                if (of_t->openfiles[i] == NULL) {
                        of = i;
                        break;
                }
        }

        /* file descriptor table and/or open file table is full */
        if (fd == -1 || of == -1) {
                vfs_close(vn);
                lock_release(of_t->oft_l);
                lock_release(fd_t->fdt_l);
                return EMFILE;
        }

        /* create new file record */
        struct open_file *of_entry = kmalloc(sizeof(struct open_file));
        if (of_entry == NULL) {
                vfs_close(vn);
                lock_release(of_t->oft_l);
                lock_release(fd_t->fdt_l);
                return ENOMEM;
        }

        /* create the lock for the file for multi processes */
        struct lock *lk = lock_create("file_lock");
        if (lk == NULL) {
                vfs_close(vn);
                kfree(of_entry);
                lock_release(of_t->oft_l);
                lock_release(fd_t->fdt_l);
                return ENOMEM;
        }

        /* initialise file descriptor entry */
        fd_t->fd_entries[fd] = of;
 
        /* make all the assignments to the file */
        of_entry->vn = vn;                          /* assign vnode */
        of_entry->fl = lk;                          /* assign file lock */
        of_entry->rc = 1;                           /* refcount starts as 1 */
        of_entry->am = flags & O_ACCMODE;           /* assign access mode */
        of_entry->os = 0;                           /* inital offset is 0 */
        of_t->openfiles[of] = of_entry;

        /* release the oft */
        lock_release(of_t->oft_l);

        /* lock the file desc table for the proc */
        lock_release(fd_t->fdt_l);

        /* assign the return value */
        *fd_ret = fd; 

        return 0;
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
        struct iovec iovec_tmp;
        struct uio uio_tmp;

        /* get exclusive access to the fdt */
        lock_acquire(curproc->fd_t->fdt_l);
        
        /* get the desired file and ensure it is actually open */
        int of_entry = curproc->fd_t->fd_entries[fd];
        if (of_entry < 0 || of_entry > OPEN_MAX) {
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* lock the lock for concurrency support */
        lock_acquire(of_t->oft_l);

        struct open_file *of = of_t->openfiles[of_entry];
        if (of == NULL) {
                // lock_release(of->fl);
                lock_release(of_t->oft_l);
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* see if the fd can be read */
        if (of->am == O_WRONLY) {
                // lock_release(of->fl);
                lock_release(of_t->oft_l);
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* get open file vnode for reading from */
        struct vnode *vn = of->vn;

        /* initialize a uio with the read flag set, pointing into our buffer */
        uio_uinit(&iovec_tmp, &uio_tmp, buf, buflen, of->os, UIO_READ);

        /* read from vnode into our uio object */
        result = VOP_READ(vn, &uio_tmp);
        if (result) {
                // lock_release(of->fl);
                lock_release(of_t->oft_l);
                lock_release(curproc->fd_t->fdt_l);
                return result;
        }

        /* set the amount of bytes read */
        *sz = uio_tmp.uio_offset - of->os;

        /* update the seek pointer in the open file */
        of->os = uio_tmp.uio_offset;

        /* release the lock of the file because we are done */
        lock_release(of_t->oft_l);
        lock_release(curproc->fd_t->fdt_l);
        
        return 0;
}

/*
 * Write system call: write to a file.
 */
int 
file_write(int fd, userptr_t buf, size_t nbytes, int *sz)
{
        int result;
        struct iovec iovec_tmp;
        struct uio uio_tmp;

        /* get exclusive access to the fdt */
        lock_acquire(curproc->fd_t->fdt_l);
        
        /* get the desired file and ensure it is actually open */
        int of_entry = curproc->fd_t->fd_entries[fd];
        if (of_entry < 0 || of_entry > OPEN_MAX) {
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* acquire lock on the open file table */
        lock_acquire(of_t->oft_l);

        struct open_file *of = of_t->openfiles[of_entry];
        if (of == NULL) {
                lock_release(of_t->oft_l);
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* see if the fd can be written to */
        if (of->am == O_RDONLY) {
                lock_release(of_t->oft_l);
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* get open file vnode for writing to */
        struct vnode *vn = of->vn;

        /* initialize a uio with the write flag set, pointing into our buffer */
        uio_uinit(&iovec_tmp, &uio_tmp, buf, nbytes, of->os, UIO_WRITE);

        /* write into vnode from our uio object */
        result = VOP_WRITE(vn, &uio_tmp);
        if (result) {
                lock_release(of_t->oft_l);
                lock_release(curproc->fd_t->fdt_l);
                return result;
        }

        /* find the number of bytes written */
        *sz = uio_tmp.uio_offset - of->os;

        /* update the seek pointer in the open file */
        of->os = uio_tmp.uio_offset;

        /* release the lock on the file */
        lock_release(of_t->oft_l);
        lock_release(curproc->fd_t->fdt_l);

        return 0;
}

/* file_close
 * closes a file described by a file descriptor
 */
int
file_close(int fd)
{
        /* check to see file to close is legit */
        if (fd < 0 || fd >= OPEN_MAX) {
                return EBADF;
        }

        /* flag to know if we have been called from another syscall */
        int calledFromSys = 1;

        if (!lock_do_i_hold(curproc->fd_t->fdt_l)) {
                /* get exclusive access to the fd table */
                calledFromSys = 0;
                lock_acquire(curproc->fd_t->fdt_l);
        }

        /* check to see the open file index is legit */
        int of_entry = curproc->fd_t->fd_entries[fd];
        if (of_entry < 0 || of_entry > OPEN_MAX) {
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* get exclusive access to the file */
        lock_acquire(of_t->oft_l);

        /* get the open file we wish to close */
        struct open_file *of = of_t->openfiles[of_entry];
        if (of == NULL) {
                lock_release(of_t->oft_l);
                lock_release(curproc->fd_t->fdt_l);
                return EBADF;
        }

        /* close the file for this process */
        curproc->fd_t->fd_entries[fd] = FILE_CLOSED;

        /* lock the open file to read from it */
        if (of->rc == 1) {
                /* free memory */
                vfs_close(of->vn);
                lock_destroy(of->fl);
                kfree(of);
        } else {
                of->rc = of->rc - 1;
        }

        /* release exclusive access to the fd table */
        lock_release(of_t->oft_l);

        /* if we weren't called from another syscall */
        if (!calledFromSys) {
                lock_release(curproc->fd_t->fdt_l);
        }

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

        /* ---- initialising global open file table, do not free --- */
        /* if there is no open file table created yet, create it */
        if (of_t == NULL) {
                of_t = kmalloc(sizeof(struct file_table));
                if (of_t == NULL) {
                        return ENOMEM;
                }

                /* create the lock for the open file table */
                struct lock *oft_lk = lock_create("of_table_lock");
                if (oft_lk == NULL) {
                        return ENOMEM;
                }

                /* acquire lock to perform operations on the open file table */
                lock_acquire(oft_lk);
                of_t->oft_l = oft_lk;

                for (i = 0; i < OPEN_MAX; i++) {
                        of_t->openfiles[i] = NULL;
                }

                lock_release(oft_lk);
        }
        /* ---- end of global file table initialisation ------------ */


        /* if there is no file descriptor table for proc - make one! */
        curproc->fd_t = kmalloc(sizeof(struct fd_table));
        if (curproc->fd_t == NULL) {
                return ENOMEM;
        }

        /* create the lock for the file for multi processes */
        struct lock *fdt_lk = lock_create("fd_table_lock");
        if (fdt_lk == NULL) {
                kfree(curproc->fd_t);
                return ENOMEM;
        }

        /* we have the only reference to the fdt so don't acquire lock */

        curproc->fd_t->fdt_l = fdt_lk;

        /* empty the new table */
        for (i = 0; i < OPEN_MAX; i++) {
                curproc->fd_t->fd_entries[i] = FILE_CLOSED;
        }

        /* create stdin file desc */
        strcpy(path, stdin_path);
        result = file_open(path, O_RDONLY, 0, &fd);
        if (result) {
                lock_destroy(fdt_lk);
                kfree(curproc->fd_t);
                return result;
        }

        /* create stdout file desc */
        strcpy(path, stdout_path);
        result = file_open(path, O_WRONLY, 0, &fd);
        if (result) {
                lock_destroy(fdt_lk);
                kfree(curproc->fd_t);
                return result;
        }

        /* create stderror file desc */
        strcpy(path, stderr_path);
        result = file_open(path, O_WRONLY, 0, &fd);
        if (result) {
                lock_destroy(fdt_lk);
                kfree(curproc->fd_t);
                return result;
        }

        return 0;
}

