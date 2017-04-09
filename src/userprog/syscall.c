#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"

#define pid_t int

/* A Lock for mutual exclusion between system calls.
   This lock must be released immediately when a thread is no longer
   running a code which must be executed in mutually exclueded state. */
static struct lock sys_lock;

static void syscall_handler (struct intr_frame *);

static void syscall_halt (void);
static void syscall_exit (int status);
static pid_t syscall_exec (const char *cmd_line);
static int syscall_wait (pid_t pid);
static bool syscall_create (const char *file, unsigned init_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);

void
syscall_init (void) 
{
  lock_init(&sys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Handler which matches appropriate handler to system calls.
   Dispatching handlers with arguments they need. */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uintptr_t *esp = f->esp;

  switch (*esp)
    {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      syscall_exit ((int) *(esp + 1));
      break;
    case SYS_EXEC:
      f->eax = syscall_exec ((const char *) *(esp + 1));
      break;
    case SYS_WAIT:
      f->eax = syscall_wait ((pid_t) *(esp + 1));
      break;
    case SYS_CREATE:
      f->eax = syscall_create ((const char *) *(esp + 1), (unsigned) *(esp + 2));
      break;
    case SYS_REMOVE:
      f->eax = syscall_remove ((const char *) *(esp + 1));
      break;
    case SYS_OPEN:
      f->eax = syscall_open ((const char *) *(esp + 1));
      break;
    case SYS_FILESIZE:
      f->eax = syscall_filesize ((int) *(esp + 1));
      break;
    case SYS_READ:
      f->eax = syscall_read ((int) *(esp + 1), (void *) *(esp + 2), (unsigned) *(esp + 3));
      break;
    case SYS_WRITE:
      f->eax = syscall_write ((int) *(esp + 1), (void *) *(esp + 2), (unsigned) *(esp + 3));
      break;
    case SYS_SEEK:
      syscall_seek ((int) *(esp + 1), (unsigned) *(esp + 2));
      break;
    case SYS_TELL:
      f->eax = syscall_tell ((int) *(esp + 1));
      break;
    case SYS_CLOSE:
      syscall_close ((int) *(esp + 1));
      break;
    default:
      thread_exit ();
    }
}

static void 
syscall_halt (void)
{

}

static void 
syscall_exit (int status)
{

}

static pid_t 
syscall_exec (const char *cmd_line)
{
  return -1;
}

static int 
syscall_wait (pid_t pid)
{
  return 0;
}

static bool 
syscall_create (const char *file, unsigned init_size)
{
  return true;
}

static bool 
syscall_remove (const char *file)
{
  return true;
}

static int 
syscall_open (const char *file)
{
  return 0;
}

static int 
syscall_filesize (int fd)
{
  return 0;
}

static int 
syscall_read (int fd, void *buffer, unsigned size)
{
  return 0;
}

static int 
syscall_write (int fd, void *buffer, unsigned size)
{
  return 0;
}

static void 
syscall_seek (int fd, unsigned position)
{

}

static unsigned 
syscall_tell (int fd)
{
  return 1;
}

static void 
syscall_close (int fd)
{

}
