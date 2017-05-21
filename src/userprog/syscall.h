#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#ifdef VM
#include "filesys/off_t.h"
#include "userprog/process.h"
#endif

void syscall_init (void);
void syscall_exit (int);

#ifdef VM
off_t mmap_write_back (struct file *, void *kpage, off_t);
void mmap_unmap_item (struct process_mmap *);
#endif

#endif /* userprog/syscall.h */
