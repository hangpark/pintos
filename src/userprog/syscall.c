#include "userprog/syscall.h"
#include <debug.h>
#include <stdio.h>
#include <syscall-nr.h>
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
#ifdef VM
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#endif

/* A Lock for mutual exclusion between system calls. */
static struct lock filesys_lock;

static int get_byte (const uint8_t *uaddr);
static uint32_t get_word (const uint32_t *uaddr);
static bool put_byte (uint8_t *udst, uint8_t byte);
static void validate_ptr_read (const uint8_t *uaddr, unsigned);
static void validate_ptr_write (uint8_t *udst, unsigned size);

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
#ifdef VM
static mapid_t syscall_mmap (int fd, void *addr);
static void syscall_munmap (mapid_t mapping);
#endif

void
syscall_init (void)
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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
#ifdef VM
    case SYS_MMAP:
      f->eax = syscall_mmap ((int) get_word (esp + 1),
                             (void *) get_word (esp + 2));
      break;
    case SYS_MUNMAP:
      syscall_munmap ((mapid_t) get_word (esp + 1));
      break;
#endif
    default:
      /* Undefined system calls. */
      thread_exit ();
      NOT_REACHED ();
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
  validate_ptr_read (cmd_line, 1);

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
  validate_ptr_read (file, 1);

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
  validate_ptr_read (file, 1);

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
  validate_ptr_read (file, 1);

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
  uint8_t *bf = (uint8_t *) buffer;
  validate_ptr_read (bf, size);
  validate_ptr_write (bf, size);

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
  validate_ptr_read (buffer, size);

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

#ifdef VM
/* Maps the file open as FD into the process’s virtual address
   space. */
static mapid_t
syscall_mmap (int fd, void *addr)
{
  off_t fault_ofs = -1;
  struct file *f = NULL;

  /* Check the validity. */
  lock_acquire (&filesys_lock);
  if (addr == NULL || !is_user_vaddr (addr) || pg_ofs (addr) != 0)
    goto fail;

  /* Get the file. */
  f = process_get_file (fd);
  if (f == NULL)
    goto fail;

  /* Reopen the file. */
  size_t size;
  f = file_reopen (f);
  if (f == NULL || (size = file_length (f)) == 0)
    goto fail;

  /* Install a new page. */
  off_t ofs;
  for (ofs = 0; (size_t) ofs < size; ofs += PGSIZE)
    {
      size_t read_bytes = (size_t) ofs + PGSIZE < size ? PGSIZE : size - ofs;
      size_t zero_bytes = PGSIZE - read_bytes;
      if (!suppl_pt_set_file (addr + ofs, f, ofs, read_bytes, zero_bytes,
                              true, true))
        {
          fault_ofs = ofs;
          goto fail;
        }
    }
  fault_ofs = size;

  /* Create a new mmap item. */
  mapid_t id = process_set_mmap (f, addr, size);
  if (id == MAP_FAILED)
    goto fail;

  /* Return mapping id. */
  lock_release (&filesys_lock);
  return id;

 fail:
  for (ofs = 0; ofs < fault_ofs; ofs += PGSIZE)
    suppl_pt_clear_page (addr + ofs);
  file_close (f);
  lock_release (&filesys_lock);
  return MAP_FAILED;
}

/* Unmaps the mapping designated by mapping, which must be
   a mapping ID returned by a previous call to mmap by
   the same process that has not yet been unmapped. */
static void
syscall_munmap (mapid_t mapping)
{
  struct process_mmap *mmap = process_get_mmap (mapping);
  if (mmap == NULL)
    return;
  mmap_unmap_item (mmap);
}

/* Wirtes data back to the original file with given offset. */
off_t
mmap_write_back (struct file *file, void *kpage, off_t ofs)
{
  lock_acquire (&filesys_lock);
  file = file_reopen (file);
  if (file == NULL)
    return -1;
  off_t writes_byte = file_write_at (file, kpage, PGSIZE, ofs);
  lock_release (&filesys_lock);
  return writes_byte;
}

/* Unmaps the mapping, which must be a mapping ID returned by
   a previous call to mmap by the same process that has not yet
   been unmapped. */
void
mmap_unmap_item (struct process_mmap *mmap)
{
  ASSERT (pg_ofs (mmap->addr) == 0);

  lock_acquire (&filesys_lock);
  
  /* Write back to the file. */
  off_t ofs;
  for (ofs = 0; (size_t) ofs < mmap->size; ofs += PGSIZE)
    {
      void *addr = mmap->addr + ofs;
      struct suppl_pte *pte = suppl_pt_get_page (addr);
      if (pte == NULL)
        continue;

      /* If page is loaded now. */
      if (pte->kpage != NULL)
        {
          if (suppl_pt_update_dirty (pte))
            file_write_at (mmap->file, pte->kpage, PGSIZE, ofs);
          frame_remove (pte->kpage);
          palloc_free_page (pte->kpage);
        }
      /* If page is on swap disk. */
      else if (pte->type == PAGE_SWAP)
        {
          if (suppl_pt_update_dirty (pte))
            {
              void *kpage = palloc_get_page (0);
              swap_in (kpage, pte->swap_index);
              file_write_at (mmap->file, pte->kpage, PGSIZE, ofs);
              palloc_free_page (kpage);
            }
          else
            swap_remove (pte->swap_index);
        }

      /* Free resources. */
      pagedir_clear_page (pte->pagedir, pte->upage);
      hash_delete (&thread_current ()->suppl_pt->hash, &pte->elem);
    }

  /* Free resources. */
  list_remove (&mmap->elem);
  file_close (mmap->file);
  free (mmap);
  lock_release (&filesys_lock);
}
#endif

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

/* Writes BYTE to user address UDST.
   Returns true if successful, false if a segfault occurred
   or UADDR is not in the user space. */
static bool
put_byte (uint8_t *udst, uint8_t byte)
{
  if (!is_user_vaddr (udst))
    return false;
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "r" (byte));
  return error_code != -1;
}

/* Validates reading to a given user virtual address UADDR up to
   size SIZE. */
static void
validate_ptr_read (const uint8_t *uaddr, size_t size)
{
  uint8_t *ptr;
  for (ptr = pg_round_down (uaddr); ptr < uaddr + size; ptr += PGSIZE)
    {
      if (get_byte (ptr) == -1)
        {
          syscall_exit (-1);
          NOT_REACHED ();
        }
    }
}

/* Validates writing to a given user virtual address UADDR up to
   size SIZE.

   Use this method after validate_ptr_read(). */
static void
validate_ptr_write (uint8_t *udst, unsigned size)
{
  uint8_t *ptr;
  for (ptr = pg_round_down (udst); ptr < udst + size; ptr += PGSIZE)
    {
      if (!put_byte (ptr, get_byte (ptr)))
        {
          syscall_exit (-1);
          NOT_REACHED ();
        }
    }
}
