#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <debug.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "threads/vaddr.h"

#define pid_t int

/* A Lock for mutual exclusion between system calls. */
static struct lock filesys_lock;

static int get_byte (const uint8_t *uaddr);
static bool put_byte (uint8_t *udst, uint8_t byte);
static uint32_t get_word (const uint32_t *uaddr);
static void validate_ptr (const uint8_t *uaddr);

static void syscall_handler (struct intr_frame *);

static void syscall_halt (void);
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
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   Returns the byte value if successful, -1 if a segfault
   occured or UADDR is not in the user space. */
static int
get_byte (const uint8_t *uaddr)
{
  if (!is_user_vaddr (uaddr))
    return -1;
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   Returns true if successful, false if a segfault occured
   or UDST is not in the user space. */
static bool
put_byte (uint8_t *udst, uint8_t byte)
{
  if (!is_user_vaddr (udst))
    return false;
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/* Reads a word at user virtual address ADDR.
   Returns the word value if successful, calls exit system
   call if not. */
static uint32_t
get_word (const uint32_t *uaddr)
{
  uint32_t res;
  int byte;
  int i;
  for (i = 0; i < 4; i++)
    {
      byte = get_byte ((uint8_t *) uaddr + i);
      if (byte == -1)
        {
          syscall_exit (-1);
          NOT_REACHED ();
        }
      *((uint8_t *) &res + i) = (uint8_t) byte;
    }
  return res;
}

/* Validates a given user virtual address. */
static void
validate_ptr (const uint8_t *uaddr)
{
  if (get_byte (uaddr) == -1)
    {
      syscall_exit (-1);
      NOT_REACHED ();
    }
}

/* Handler which matches the appropriate system call. */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t *esp = f->esp;

  switch (get_word (esp))
    {
    case SYS_HALT:
      syscall_halt ();
      break;
    case SYS_EXIT:
      syscall_exit ((int) get_word (esp + 1));
      break;
    case SYS_EXEC:
      f->eax = syscall_exec ((const char *) get_word (esp + 1));
      break;
    case SYS_WAIT:
      f->eax = syscall_wait ((pid_t) get_word (esp + 1));
      break;
    case SYS_CREATE:
      f->eax = syscall_create ((const char *) get_word (esp + 1),
                               (unsigned) get_word (esp + 2));
      break;
    case SYS_REMOVE:
      f->eax = syscall_remove ((const char *) get_word (esp + 1));
      break;
    case SYS_OPEN:
      f->eax = syscall_open ((const char *) get_word (esp + 1));
      break;
    case SYS_FILESIZE:
      f->eax = syscall_filesize ((int) get_word (esp + 1));
      break;
    case SYS_READ:
      f->eax = syscall_read ((int) get_word (esp + 1),
                             (void *) get_word (esp + 2),
                             (unsigned) get_word (esp + 3));
      break;
    case SYS_WRITE:
      f->eax = syscall_write ((int) get_word (esp + 1),
                              (void *) get_word (esp + 2),
                              (unsigned) get_word (esp + 3));
      break;
    case SYS_SEEK:
      syscall_seek ((int) get_word (esp + 1),
                    (unsigned) get_word (esp + 2));
      break;
    case SYS_TELL:
      f->eax = syscall_tell ((int) get_word (esp + 1));
      break;
    case SYS_CLOSE:
      syscall_close ((int) get_word (esp + 1));
      break;
    default:
      /* Undefined system calls. */
      thread_exit ();
    }
}

/* Terminates pintos. */
static void 
syscall_halt (void)
{
  power_off ();
  NOT_REACHED ();
}

/* Terminates the current user program, returning status
   to the kernel. */
void
syscall_exit (int status)
{
  /* Set exit code. */
  process_current ()->exit_code = status;

  /* Print the termination message. */
  printf ("%s: exit(%d)\n", thread_current ()->name, status);

  /* Exit the current thread. */
  thread_exit ();
  NOT_REACHED ();
}

/* Runs the executable whose name is given in cmd_line,
   passing any given arguments, and returns the new process's
   process id (pid). */
static pid_t 
syscall_exec (const char *cmd_line)
{
  /* Check the validity. */
  validate_ptr (cmd_line);

  /* Create a new process. */
  pid_t pid = process_execute (cmd_line);
  if (pid == PID_ERROR)
    return pid;

  /* Obtain the new process. */
  struct process *p = process_find_child (process_current (), pid);
  if (p == NULL)
    return PID_ERROR;

  /* Wait until the new process is successfully loaded. */
  while (p->status == PROCESS_LOADING)
    thread_yield ();

  /* Return PID. */
  if (p->status & PROCESS_FAIL)
    return PID_ERROR;
  return p->pid;
}

/* Waits for a child process pid and retrieves the child's exit status. */
static int 
syscall_wait (pid_t pid)
{
  return process_wait (pid);
}

/* Creates a new file initially the given bytes in size.
   Returns true if successful, false otherwise. */
static bool 
syscall_create (const char *file, unsigned init_size)
{
  /* Check the validity. */
  validate_ptr (file);

  /* Create a new file. */
  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, init_size);
  lock_release (&filesys_lock);
  return success;
}

/* Deletes the file. Returns true if successful, false otherwise. */
static bool 
syscall_remove (const char *file)
{
  /* Check the validity. */
  validate_ptr (file);

  /* Create a new file. */
  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

/* Opens the file. Returns a nonnegative integer handle,
   a file descriptor, or -1 if the file could not be opened. */
static int 
syscall_open (const char *file)
{
  /* Check the validity. */
  validate_ptr (file);

  /* Open the file. */
  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  if (f == NULL)
    {
      lock_release (&filesys_lock);
      return -1;
    }

  /* Set the file. */
  int fd = process_set_file (f);

  /* Return the file descriptor. */
  lock_release (&filesys_lock);
  return fd;
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
  if (fd == STDOUT_FILENO)
    putbuf (buffer, size);
  return size;
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

/* Closes file descriptor. */
static void 
syscall_close (int fd)
{
  lock_acquire (&filesys_lock);
  struct list *list = &process_current ()->file_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
    {
      struct process_file *pfe = list_entry (e, struct process_file, elem);
      if (pfe->fd == fd)
        {
          file_close (pfe->file);
          list_remove (e);
          free (pfe);
          lock_release (&filesys_lock);
          return;
        }
    }
  lock_release (&filesys_lock);
}
