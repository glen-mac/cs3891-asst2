/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>

/* TO NOTE:
 * A file table can be owned by one process only, whereas an open file
 * may be owned by many processes - for example when a process forks().
 * Hence it is important to include a synchronisation primitive in the 
 * struct for the open file to be able to lock the use of it.
 */

struct open_file {
	struct vnode *vn;		/* the vnode this file represents */
	struct lock *fl;		/* file lock for accessing */
	int am;					/* the access mode of this file	*/
	int os;					/* the read offset within this file	*/
	int rc;					/* the reference count of this file */
};

struct file_table {
	struct open_file *openfiles[OPEN_MAX];	/* array of open files */
};

/* opens a file using the VFS and stores the result in the thread file table */
int file_open(char *filename, int flags, mode_t mode, int *fd);

/* reads from a file and stores the result in buf */
int file_read(int fd, userptr_t buf, size_t buflen, int *sz);

/* checks if a table exists for the current thread and creates one */
int file_table_init(const char *stdin_path, const char *stdout_path,
		const char *stderr_path);

#endif /* _FILE_H_ */
