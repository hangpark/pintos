#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "process.h"

void syscall_init (void);
void syscall_exit (int);

void unmap_mmap_item (struct process_mmap *);

#endif /* userprog/syscall.h */
