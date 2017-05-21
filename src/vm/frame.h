#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "vm/page.h"

/* Frame table element. */
struct frame
  {
    void *kpage;                  /* Kernel page maps to the frame. */
    struct suppl_pte *suppl_pte;  /* Supplemental page table entry. */
    struct list_elem elem;        /* List element. */
  };

void frame_table_init (void);
struct frame *frame_alloc (struct suppl_pte *, enum palloc_flags);
void frame_free (struct frame *);
void frame_remove (void *kpage);
void frame_append (struct frame *);

#endif /* vm/frame.h */
