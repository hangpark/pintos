#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#ifdef VM
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#endif

#define FD_MIN 2            /* Min value for file descriptors. */
#ifdef VM
#define MAPID_MIN 0         /* Min value for memory mapped identifiers. */
#endif

/* Structure for arguments. */
struct arguments
  {
    int argc;               /* Number of arguments. */
    char **argv;            /* Array of arguments. */
  };

static struct arguments *parse_arguments(char *str_input);
static thread_func start_process NO_RETURN;
static bool load (struct arguments *args, void (**eip) (void), void **esp,
                  struct file **exec_file);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
pid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  struct arguments *args;
  pid_t pid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return PID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Parse arguments into the structure. */
  args = parse_arguments (fn_copy);
  if (args == NULL)
    {
      free (args);
      palloc_free_page (fn_copy);
      return PID_ERROR;
    }

  /* Create a new thread to execute the given file name. */
  pid = (pid_t) thread_create (args->argv[0], PRI_DEFAULT, start_process, args);
  if (pid == TID_ERROR)
    {
      palloc_free_page (args->argv);
      free(args);
      palloc_free_page (fn_copy);
    }
  return pid;
}

/* Parses arguments from the given string by replacing whole
   adjacent spaces to the null character. Then stores it in
   given arguments struct. Returns the pointer of arguments
   struct if successful, NULL otherwise. */
static struct arguments *
parse_arguments (char *str_input)
{
  struct arguments *args;
  char *leftover;
  char *curr;

  args = (struct arguments *) malloc (sizeof (struct arguments));
  if (args == NULL)
    return NULL;

  args->argc = 0;
  args->argv = (char **) palloc_get_page (0);
  if (args->argv == NULL)
    {
      free (args);
      return NULL;
    }

  for (curr = strtok_r (str_input, " ", &leftover); curr != NULL;
       curr = strtok_r (NULL, " ", &leftover))
     args->argv[args->argc ++] = curr;
  return args;
}

/* A thread function that loads a user process and makes it start
   running. */
static void
start_process (void *arguments)
{
  struct arguments *args = arguments;
  struct file *exec_file = NULL;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (args, &if_.eip, &if_.esp, &exec_file);

  /* Save load result. */
  struct process *curr = process_current ();
  if (curr->info != NULL)
    {
      if (success)
        curr->info->status |= PROCESS_RUNNING;
      else
        curr->info->status |= PROCESS_FAIL;
    }
  curr->exec_file = exec_file;
  curr->fd_next = FD_MIN;
#ifdef VM
  curr->mapid_next = MAPID_MIN;
#endif

  /* Free resources. */
  palloc_free_page (args->argv[0]);
  palloc_free_page (args->argv);
  free (args);

  /* If load failed, quit. */
  if (!success)
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread PID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If PID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given PID, returns -1
   immediately, without waiting. */
int
process_wait (pid_t child_pid)
{
  struct process_info *child = process_find_child (child_pid);

  if (child == NULL || child->is_waiting)
    return -1;
  child->is_waiting = true;
  while (!(child->status & PROCESS_EXIT))
    thread_yield ();

  int exit_code = child->exit_code;
  list_remove (&child->elem);
  free (child);
  return exit_code;
}

/* Frees the current process's resources. */
void
process_exit (void)
{
  struct process *proc = process_current ();

  /* Inform exit to child processes. */
  struct list_elem *e;
  for (e = list_begin (&proc->child_list); e != list_end (&proc->child_list);)
    {
      struct process_info *child = list_entry (e, struct process_info, elem);
      if (!(child->status & PROCESS_EXIT))
        {
          child->process->parent = NULL;
          child->process->info = NULL;
        }
      e = list_next (e);
      free (child);
    }

  /* Update the process status and free resources. */
  if (proc->info != NULL)
    proc->info->status |= PROCESS_EXIT;
  file_close (proc->exec_file);
  for (e = list_begin (&proc->file_list); e != list_end (&proc->file_list);)
    {
      struct process_file *pfe = list_entry (e, struct process_file, elem);
      e = list_next (e);
      file_close (pfe->file);
      free (pfe);
    }
#ifdef VM
  for (e = list_begin (&proc->mmap_list); e != list_end (&proc->mmap_list);)
    {
      struct process_mmap *mmap = list_entry (e, struct process_mmap, elem);
      e = list_next (e);
      mmap_unmap_item (mmap);
    }
#endif

  struct thread *curr = thread_current ();
  uint32_t *pd;

#ifdef VM
  /* Destroy the current process's supplemental page table. */
  struct suppl_pt *pt = curr->suppl_pt;
  if (pt != NULL)
    suppl_pt_destroy (pt);
#endif

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = curr->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      curr->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* Returns the current process. */
struct process *
process_current (void)
{
  return &thread_current ()->process;
}

/* Returns the child process of the current process
   with the given pid value. */
struct process_info *
process_find_child (pid_t pid)
{
  struct list *list = &process_current ()->child_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
    {
      struct process_info *child = list_entry (e, struct process_info, elem);
      if (child->pid == pid)
        return child;
    }
  return NULL;
}

/* Returns a process' file by the file descriptor. */
struct file *
process_get_file (int fd)
{
  struct list *list = &process_current ()->file_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
    {
      struct process_file *pfe = list_entry (e, struct process_file, elem);
      if (pfe->fd == fd)
        return pfe->file;
    }
  return NULL;
}

/* Sets the file into the current process and returns the file descriptor. */
int
process_set_file (struct file *file)
{
  /* Create a file element. */
  struct process_file *pfe;
  pfe = (struct process_file *) malloc (sizeof (struct process_file));
  if (pfe == NULL)
    return -1;

  /* Initialize the file element. */
  struct process *curr = process_current ();
  pfe->fd = curr->fd_next++;
  pfe->file = file;
  list_push_back (&curr->file_list, &pfe->elem);

  /* Return the file descriptor. */
  return pfe->fd;
}

#ifdef VM
/* Returns a process' memory mapped file by its identifier. */
struct process_mmap *
process_get_mmap (mapid_t id)
{
  struct list *list = &process_current ()->mmap_list;
  struct list_elem *e;
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
    {
      struct process_mmap *mmap = list_entry (e, struct process_mmap, elem);
      if (mmap->id == id)
        return mmap;
    }
  return NULL;
}

/* Sets the memory mapped file information into
   the current process and returns its identifier. */
mapid_t
process_set_mmap (struct file *file, void *addr, size_t size)
{
  struct process_mmap *mmap = malloc (sizeof (struct process_mmap));
  if (mmap == NULL)
    return MAP_FAILED;

  struct process *curr = process_current ();
  mmap->id = curr->mapid_next++;
  mmap->file = file;
  mmap->addr = addr;
  mmap->size = size;
  list_push_back (&curr->mmap_list, &mmap->elem);

  return mmap->id;
}
#endif

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (struct arguments *args, void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
static void *push_args_on_stack(struct arguments *args);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (struct arguments *args, void (**eip) (void), void **esp,
      struct file **exec_file)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  char *file_name = args->argv[0];
  off_t file_ofs;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto fail;
  process_activate ();

#ifdef VM
  /* Allocate supplemental page table. */
  t->suppl_pt = suppl_pt_create ();
  if (t->suppl_pt == NULL)
    goto fail;
#endif

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto fail;
    }

  /* Deny writing to executable file. */
  file_deny_write (file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto fail;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto fail;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto fail;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto fail;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto fail;
            }
          else
            goto fail;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (args, esp))
    goto fail;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  /* Save the executable file. */
  *exec_file = file;

  return true;

 fail:
  /* We arrive here when the load is failed. */
  file_close (file);
  return false;
}

/* load() helpers. */

#ifndef VM
static bool install_page (void *upage, void *kpage, bool writable);
#endif

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Do calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

#ifdef VM
      if (!suppl_pt_set_file (upage, file, ofs, page_read_bytes,
                              page_zero_bytes, writable, false))
        return false;
#else
      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }
#endif

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
#ifdef VM
      ofs += PGSIZE;
#endif
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (struct arguments *args, void **esp)
{
#ifdef VM
  if (!suppl_pt_set_zero (PHYS_BASE - PGSIZE))
    return false;
  *esp = push_args_on_stack (args);
  if (*esp == NULL)
    {
      suppl_pt_clear_page (PHYS_BASE - PGSIZE);
      return false;
    }
  return true;
#else
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        {
          *esp = push_args_on_stack (args);
          if (*esp == NULL)
            success = false;
        }
      if (!success)
        palloc_free_page (kpage);
    }
  return success;
#endif
}

#ifndef VM
/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
#endif

/* Push arguments on newly initialized stack. Returns pointer
   that ESP should point to if successful, NULL otherwise. */
static void *
push_args_on_stack(struct arguments *args)
{
  uint32_t **arg_ptrs = (uint32_t **) calloc (args->argc, sizeof (uint32_t *));
  if (arg_ptrs == NULL)
    return NULL;
  uint8_t *curr8 = (uint8_t *) PHYS_BASE;
  int i;

  /* Push arguments on stack, save their address in arg_ptrs. */
  int arg_len;
  for (i = args->argc - 1; i >= 0; i--)
    {
      arg_len = strlen (args->argv[i]) + 1;
      curr8 -= arg_len;
      memcpy (curr8, args->argv[i], arg_len);
      arg_ptrs[i] = (uint32_t *) curr8;
    }

  /* Align, and push bytes required for aligning. */
  int alignment = ((uint32_t) curr8) % sizeof (uintptr_t);
  for (i = 0; i < alignment ; i++)
     *--curr8 = 0;

  /* Insert pointer to argv[argc], which must be empty. */
  uint32_t *curr32 = (uint32_t *) curr8;
  *--curr32 = 0;

  /* Push a pointer to argv[argc-1], argv[argc-2] ...
     until a pointer to argv[0] is pushed. */
  for (i = args->argc - 1; i >= 0; i--)
    *--curr32 = (uint32_t) arg_ptrs[i];

  /* Push address of argv on stack. */
  --curr32;
  *curr32 = (uint32_t) (curr32 + 1);

  /* Push argument count on stack. */
  *--curr32 = (uint32_t) args->argc;

  /* Push return address as 0 values. */
  *--curr32 = 0;

  /* Return result. */
  free (arg_ptrs);
  return curr32;
}
