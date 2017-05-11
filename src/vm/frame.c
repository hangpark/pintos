/* Set default page replacement algorithm with clock algorithm. */
#if !defined(VM_CLOCK) && !defined(VM_FIFO)
#define VM_CLOCK
#endif

#include "vm/frame.h"
#include <bitmap.h>
#include <debug.h>
#include <list.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"

/* Frame table lock. */
struct lock frame_table_lock;

/* Frame table. */
struct list frame_table;

#ifdef VM_CLOCK
/* Current frame table position. */
struct list_elem *frame_table_pos;
#endif

/* Frame table element. */
struct frame
  {
    void *kpage;                  /* Kernel page maps to the frame. */
    struct suppl_pte *suppl_pte;  /* Supplemental page table entry. */
    struct list_elem elem;        /* List element. */
  };

static struct frame *frame_evict_and_get (void);
#ifdef VM_CLOCK
static struct frame *frame_to_evict_clock (void);
#elif VM_FIFO
static struct frame *frame_to_evict_fifo (void);
#endif

/* Initializes the frame table and its lock. */
void
frame_table_init (void)
{
  lock_init (&frame_table_lock);
  list_init (&frame_table);
#ifdef VM_CLOCK
  frame_table_pos = list_tail (&frame_table);
#endif
}

/* Allocates a new user frame with given pte and flags.
   Note that this method is used only for that user frame flag
   is set. */
void *
frame_alloc (struct suppl_pte *pte, enum palloc_flags flags)
{
  ASSERT (pte != NULL);
  ASSERT (flags & PAL_USER);

  lock_acquire (&frame_table_lock);

  struct frame *f;
  void *kpage = palloc_get_page (flags); 
  if (kpage == NULL)
    {
      f = frame_evict_and_get ();
      if (f == NULL)
        {
          lock_release (&frame_table_lock);
          return NULL;
        }
      f->suppl_pte = pte;

      lock_release (&frame_table_lock);

      return f->kpage;
    }

  f = malloc (sizeof (struct frame));
  if (f == NULL)
    {
      palloc_free_page (kpage);
      lock_release (&frame_table_lock);
      return NULL;
    }

  f->kpage = kpage;
  f->suppl_pte = pte;
  list_push_back (&frame_table, &f->elem);

  lock_release (&frame_table_lock);

  return kpage;
}

/* Returns frame associated KPAGE. Returns NULL if failed. */
static struct frame *
frame_search (void *kpage)
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));

  struct frame *f = NULL;
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct frame *f_ = list_entry (e, struct frame, elem);
      if (f_->kpage == kpage)
        {
          f = f_;
          break;
        }
    }

  return f;
}

/* Frees the given frame at kpage.
   If such frame exists in the frame table, remove it. */
void
frame_free (void *kpage)
{
  lock_acquire (&frame_table_lock);

  struct frame *f = frame_search (kpage);
  if (f != NULL)
    {
#ifdef VM_CLOCK
      if (&f->elem == frame_table_pos)
        frame_table_pos = list_next (frame_table_pos);
#endif
      list_remove (&f->elem);
      free (f);
    }
  palloc_free_page (kpage);

  lock_release (&frame_table_lock);
}

/* Removes the frame table entry associated with KPAGE without
   freeing it. */
void
frame_remove (void *kpage)
{
  lock_acquire (&frame_table_lock);

  struct frame *f = frame_search (kpage);
  if (f != NULL)
    {
#ifdef VM_CLOCK
      if (&f->elem == frame_table_pos)
        frame_table_pos = list_next (frame_table_pos);
#endif
      list_remove (&f->elem);
      free (f);
    }

  lock_release (&frame_table_lock);
}

static struct frame *
frame_evict_and_get (void)
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));

#ifdef VM_CLOCK
  struct frame *f = frame_to_evict_clock ();
#elif VM_FIFO
  struct frame *f = frame_to_evict_fifo ();
#endif
  if (f == NULL)
    return NULL;

  ASSERT (f->suppl_pte != NULL);

  size_t idx;
  struct suppl_pte *pte = f->suppl_pte;
  switch (pte->type)
    {
    case PAGE_FILE:
      if (!pte->writable)
        break;

    case PAGE_ZERO:
      if (!suppl_pt_update_dirty (pte))
        break;

    case PAGE_SWAP:
      idx = swap_out (f->kpage);
      if (idx == BITMAP_ERROR)
        return NULL;
      pte->type = PAGE_SWAP;
      pte->swap_index = idx;
      break;

    /* Unintended type. */
    default:
      NOT_REACHED ();
    }

  /* Save dirty bit. */
  suppl_pt_update_dirty (pte);

  /* Uninstall frame. */
  pte->kpage = NULL;
  pagedir_clear_page (pte->pagedir, pte->upage);

  return f;
}

#ifdef VM_CLOCK
/* Returns the next frame in the frame table as circular list. */
static struct frame *
frame_next_circ (void)
{
  ASSERT (!list_empty (&frame_table));

  struct list_elem *next;
  if (frame_table_pos == list_back (&frame_table)
      || frame_table_pos == list_tail (&frame_table))
    next = list_front (&frame_table);
  else
    next = list_next (frame_table_pos);
  frame_table_pos = next;
  return list_entry (next, struct frame, elem);
}

/* Returns the frame to be evicted.

   This implements the clock algorithm. */
static struct frame *
frame_to_evict_clock (void)
{
  struct frame *f;
  for (f = frame_next_circ ();
       pagedir_is_accessed (f->suppl_pte->pagedir, f->suppl_pte->upage);
       f = frame_next_circ ())
    pagedir_set_accessed (f->suppl_pte->pagedir, f->suppl_pte->upage, false);
  return f;
}
#elif VM_FIFO
/* Returns the frame to be evicted.

   This implements the FIFO algorithm. You can use this instead of
   the clock algorithm by giving VM_FIFO option to the compiler. */
static struct frame *
frame_to_evict_fifo (void)
{
  struct frame *f = list_entry (list_begin (&frame_table), struct frame, elem);
  list_remove (&f->elem);
  list_push_back (&frame_table, &f->elem);
  return f;
}
#endif
