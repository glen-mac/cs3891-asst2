#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <file.h>
#include <copyinout.h>

/* 
 * Code is adapted from runprogram.c
 */

#define FREE_ALLOCS() kfree(argvp);kfree(argv);kfree(userland_argvp);

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
sys_execv(userptr_t userprog, userptr_t *userargs)
{
        /* initial check for the arguments */
        if (userargs == NULL || userprog == NULL) {
                return EFAULT;
        }

        /* check for kernel pointers */
        if ((vaddr_t)(userprog) >= USERSPACETOP || 
                        (vaddr_t)(userargs) >= USERSPACETOP) {
                return EFAULT;
        }

        struct addrspace *as;
        struct vnode *v;
        vaddr_t entrypoint, stackptr;
        int result;
        int i, argv_index, argc;
        size_t argv_length;
        char **argvp;
        char *argv;
        vaddr_t **userland_argvp;
        char progname[PATH_MAX];
        // char *tmp;
        int tmp;

        /* count arguments and store in argc */
        i = 0;
        while ((userargs+i)!=NULL) {
                /* we were given a kernel address */
                if ((vaddr_t)(userargs+1) >= USERSPACETOP) {
                        return EFAULT;
                }
                /* end of arg list */
                if (*(userargs+i) == NULL) {
                        break;
                }
                i += 1;
        }
        argc = i;           // plus one for program name

        /* initialise with max size of arguments */
        argv = kmalloc(sizeof(char) * ARG_MAX);
        if (argv == NULL) {
                return ENOMEM;
        }

        /* keep track of argv pointers */
        argvp = kmalloc(sizeof(char *) * argc);
        if (argvp == NULL) {
                kfree(argv);
                return ENOMEM;
        }

        /* keep track of userland argv pointers */
        userland_argvp = kmalloc(sizeof(vaddr_t) * argc);
        if (userland_argvp == NULL){
                kfree(argvp);
                kfree(argv);
                return ENOMEM;
        }

        i = 0;
        argv_index = 0;
        argv_length = 0;

        /* copy in program name */
        result = copyinstr(userprog, &argv[argv_index], PATH_MAX, &argv_length);
        if (result) {
                FREE_ALLOCS();
                return result;
        }

        /* check program name length */
        if (argv_length == 0) {
                FREE_ALLOCS();
                return ENOEXEC;
        }

        /* copy in arg vectors from userland
         *
         * argv_length : strlen of current vector being read in
         * argv_index  : index to store next arg vector in (also total
         *                      strlen of arg vectors)
         */
        /* the userargs is null terminated, so check for this */
        while (*(userargs+i) != NULL) {

                /* read in string only up to the max space available */
                result = copyinstr(*(userargs+i), &argv[argv_index], (ARG_MAX - argv_length), &argv_length);
                if (result) {
                        FREE_ALLOCS();
                        return result;
                }

                /* Check if the total length of the argument vectors exceeds ARG_MAX */
                if ((argv_index + argv_length - 1) >= ARG_MAX) {
                        FREE_ALLOCS();
                        return E2BIG;
                }

                /* store pointer to string in kernel space */
                argvp[i] = argv + argv_index;

                /* increment next index to store next vector at */
                argv_index += argv_length;

                i += 1;
        }


        /* init the file descriptor table for the process */
        result = file_table_init("con:", "con:", "con:");
        if (result) {
                FREE_ALLOCS();
                return result;
        }

        /* read in program name to open */
        result = copyinstr(userprog, progname, PATH_MAX, &argv_length);
        if (result) {
                FREE_ALLOCS();
                return result;
        }


        /* Open the file. */
        result = vfs_open(progname, O_RDONLY, 0, &v);
        if (result) {
                FREE_ALLOCS();
                return result;
        }

        /* We should NOT be a new process. */
        KASSERT(proc_getas() != NULL);

        /* Create a new address space. */
        as = as_create();
        if (as == NULL) {
                FREE_ALLOCS();
                vfs_close(v);
                return ENOMEM;
        }

        /* Switch to it and activate it. */
        struct addrspace *oldas = proc_setas(as);
        as_activate();


        /* Load the executable. */
        result = load_elf(v, &entrypoint);
        if (result) {
                proc_setas(oldas);
                as_destroy(as);
                FREE_ALLOCS();
                /* p_addrspace will go away when curproc is destroyed */
                vfs_close(v);
                return result;
        }

        /* Done with the file now. */
        vfs_close(v);

        /* Define the user stack in the address space */
        result = as_define_stack(as, &stackptr);
        if (result) {
                proc_setas(oldas);
                as_destroy(as);
                FREE_ALLOCS();
                /* p_addrspace will go away when curproc is destroyed */
                return result;
        }

        /* place null as the last thing on the stack */
        stackptr -= sizeof(vaddr_t);
        result = copyout("\0\0\0\0", (userptr_t)stackptr, sizeof(vaddr_t));
        if (result) {
                proc_setas(oldas);
                as_destroy(as);
                FREE_ALLOCS();
                return result;
        }

        /* copy arguments to new process' stack */
        i = argc - 1;
        while (i >= 0) {

                /* string length of current argument vector */
                tmp = strlen(argvp[i]) + 1;     // +1 for '\0'

                /* decrement stack pointer and save it */
                //  stackptr -= tmp;
                stackptr = (vaddr_t)((int)stackptr - tmp);

                /* save userland pointer to arg vector */
                userland_argvp[i] = (void *)stackptr;

                /* copy string to userland */
                copyoutstr(argvp[i], (userptr_t)stackptr, tmp, NULL);
                if (result) {
                        proc_setas(oldas);
                        as_destroy(as);
                        FREE_ALLOCS();
                        return result;
                }

                /* move to next vector to place on stack (in reverse order) */
                i -= 1;
        }

        /* align stackptr */
        stackptr -= 4;
        if (stackptr % 4 != 0) {
                stackptr -= stackptr % 4;
        }


        /* null terminate array of pointers */
        result = copyout("\0\0\0\0", (userptr_t)stackptr, sizeof(vaddr_t));
        if (result) {
                proc_setas(oldas);
                as_destroy(as);
                FREE_ALLOCS();
                return result;
        }

        stackptr -= sizeof(vaddr_t);


        i = argc - 1;

        /* place userland pointers to arguments on new process' stack */
        while (i >= 0) {

                result = copyout(&userland_argvp[i], (userptr_t)stackptr, sizeof(vaddr_t));
                if (result) {
                        proc_setas(oldas);
                        as_destroy(as);
                        FREE_ALLOCS();
                        return result;
                }

                stackptr -= 4;
                i -= 1;
        }

        /* keep track of userland argv[] */
        userptr_t argv_ptr = (userptr_t) stackptr + 4;

        /* decrement stack pointer to next available spot on stack */
        // (this will actually give 1 space buffer between argument pointers)
        stackptr -= sizeof(vaddr_t);

        /* destroy old as */
        as_destroy(oldas);

        /* free kmallocs */
        FREE_ALLOCS();

        /* Warp to user mode. */
        enter_new_process(argc, argv_ptr,
                        NULL /*userspace addr of environment*/,
                        stackptr, entrypoint);

        /* enter_new_process does not return. */
        panic("enter_new_process returned\n");
        return EINVAL;
}


