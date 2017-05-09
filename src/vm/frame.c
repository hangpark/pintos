#include "vm/frame.h"
#include <debug.h>
#include <list.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"

/* Frame table lock. */
struct lock frame_table_lock;

/* Frame table. */
struct list frame_table;

/* Frame table element. */
struct frame
  {
    void *kpage;                /* Kernel page maps to the physical address. */
    struct list_elem elem;      /* List element. */
  };

/* Initializes the frame table and its lock. */
void
frame_table_init (void)
{
  lock_init (&frame_table_lock);
  list_init (&frame_table);
}

/* Allocates a new user frame with given flags.
   Note that this method is used only for that user frame flag
   is set. */
void *
frame_alloc (enum palloc_flags flags)
{
  ASSERT (flags & PAL_USER);

  lock_acquire (&frame_table_lock);

  void *kpage = palloc_get_page (flags); 
  if (kpage == NULL)
    {
      lock_release (&frame_table_lock);
      return NULL;
    }

  struct frame *f = malloc (sizeof (struct frame));
  if (f == NULL)
    {
      palloc_free_page (kpage);
      return NULL;
    }

  f->kpage = kpage;
  list_push_back (&frame_table, &f->elem);

  lock_release (&frame_table_lock);

  return kpage;
}


static struct frame *
frame_search (void *kpage)
{
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
      list_remove (&f->elem);
      free (f);
    }
  palloc_free_page (kpage);

  lock_release (&frame_table_lock);
}

void
frame_remove (void *kpage)
{
  lock_acquire (&frame_table_lock);

  struct frame *f = frame_search (kpage);
  if (f != NULL)
    {
      list_remove (&f->elem);
      free (f);
    }

  lock_release (&frame_table_lock);
}
