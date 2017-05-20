#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#ifdef VM
#include "process.h"
#endif

void syscall_init (void);
void syscall_exit (int);

#ifdef VM
void unmap_mmap_item (struct process_mmap *);
#endif

#endif /* userprog/syscall.h */
