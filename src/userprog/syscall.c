#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <debug.h>
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

/* A Lock for mutual exclusion between system calls. */
static struct lock filesys_lock;

static int get_byte (const uint8_t *uaddr);
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
  thread_current ()->esp = esp;

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
  if (process_current ()->info != NULL)
    process_current ()->info->exit_code = status;

  /* Print the termination message. */
  printf ("%s: exit(%d)\n", thread_current ()->name, status);

  /* Exit the current thread. */
  thread_exit ();
  NOT_REACHED ();
}

/* Runs the executable whose name is given in cmd_line,
   passing any given arguments, and returns the new process's
   process id. */
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
  struct process_info *child = process_find_child (pid);
  if (child == NULL)
    return PID_ERROR;

  /* Wait until the new process is successfully loaded. */
  while (child->status == PROCESS_LOADING)
    thread_yield ();

  /* Return PID. */
  if (child->status & PROCESS_FAIL)
    return PID_ERROR;
  return pid;
}

/* Waits for a child process PID and retrieves the child's exit status. */
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

/* Returns the size, in bytes, of the file open as fd. */
static int
syscall_filesize (int fd)
{
  /* Open the file. */
  lock_acquire (&filesys_lock);
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      return -1;
    }

  /* Get the size of the file. */
  int size = file_length (file);

  /* Return the size of the file. */
  lock_release (&filesys_lock);
  return size;
}

/* Reads size bytes from the file open as fd into buffer.
   Returns the number of bytes actually read, or -1
   if the file could not be read. */
static int
syscall_read (int fd, void *buffer, unsigned size)
{
  /* Check the validity. */
  validate_ptr (buffer);
  uint8_t *bf = (uint8_t *) buffer;

  /* Read from STDIN. */
  unsigned bytes = 0;
  if (fd == STDIN_FILENO)
    {
      uint8_t b;
      while (bytes < size && (b = input_getc ()) != 0)
        {
          *bf++ = b;
          bytes ++;
        }
      return (int) bytes;
    }

  /* Get the file. */
  lock_acquire (&filesys_lock);
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      return -1;
    }

  /* Read from the file. */
  bytes = file_read (file, buffer, size);

  /* Return bytes read. */
  lock_release (&filesys_lock);
  return (int) bytes;
}

/* Writes size bytes from buffer to the open file fd.
   Returns the number of bytes actually written. */
static int
syscall_write (int fd, void *buffer, unsigned size)
{
  /* Check the validity. */
  validate_ptr (buffer);

  /* Write to STDOUT. */
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }

  /* Get the file. */
  lock_acquire (&filesys_lock);
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      return -1;
    }

  /* Write to the file. */
  int bytes = file_write (file, buffer, size);

  /* Return bytes written. */
  lock_release (&filesys_lock);
  return bytes;
}

/* Changes the next byte to be read or written in open file fd
   to position, expressed in bytes from the beginning of the file. */
static void
syscall_seek (int fd, unsigned position)
{
  /* Open the file. */
  lock_acquire (&filesys_lock);
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      return;
    }

  /* Seek the file. */
  file_seek (file, position);

  lock_release (&filesys_lock);
}

/* Returns the position of the next byte to be read or written
   in open file fd, expressed in bytes from the beginning
   of the file. */
static unsigned
syscall_tell (int fd)
{
  /* Open the file. */
  lock_acquire (&filesys_lock);
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      return -1;
    }

  /* Get the position. */
  unsigned position = file_tell (file);

  /* Return the position. */
  lock_release (&filesys_lock);
  return position;
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
