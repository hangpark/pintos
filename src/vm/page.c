#include "vm/page.h"
#include <list.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"

static void suppl_pt_free_pte (struct suppl_pte *);

/* Creates and returns a new supplemental page table. */
struct suppl_pt *
suppl_pt_create (void)
{
  struct suppl_pt *pt = malloc (sizeof (struct suppl_pt *));
  if (pt == NULL)
    return NULL;

  list_init (&pt->list);

  return pt;
}

/* Destroys the given supplemental page table PT.
   Frees its supplemental page table entries and removes frame
   table entries but not frees frame table entries since they
   will be freed by pagedir_destroy(). */
void
suppl_pt_destroy (struct suppl_pt *pt)
{
  struct list_elem *e;
  for (e = list_begin (&pt->list); e != list_end (&pt->list);)
    {
       struct suppl_pte *pte = list_entry (e, struct suppl_pte, elem); 
       e = list_next (e);
       suppl_pt_free_pte (pte);
    }
  free (pt);
}

/* Adds a new supplemental page table entry of zero-fill with
   user virtual page UPAGE.
   Note that this does not involve actual frame allocation. */
bool
suppl_pt_set_zero (void *upage)
{
  struct suppl_pte *pte = malloc (sizeof (struct suppl_pte));
  if (pte == NULL)
    return false;

  pte->type = PAGE_ZERO;
  pte->upage = upage;
  pte->kpage = NULL;
  pte->pagedir = thread_current ()->pagedir;
  pte->dirty = false;

  struct suppl_pt *pt = thread_current ()->suppl_pt;
  list_push_back (&pt->list, &pte->elem);

  return true;
}

/* Adds a new supplemental page table entry from file system with
   user virtual page UPAGE.
   Note that this does not involve actual frame allocation. */
bool
suppl_pt_set_file (void *upage, struct file *file, off_t ofs,
                   uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct suppl_pte *pte = malloc (sizeof (struct suppl_pte));
  if (pte == NULL)
    return false;

  pte->type = PAGE_FILE;
  pte->upage = upage;
  pte->kpage = NULL;
  pte->pagedir = thread_current ()->pagedir;
  pte->dirty = false;
  pte->file = file;
  pte->ofs = ofs;
  pte->read_bytes = read_bytes;
  pte->zero_bytes = zero_bytes;
  pte->writable = writable;

  struct suppl_pt *pt = thread_current ()->suppl_pt;
  list_push_back (&pt->list, &pte->elem);

  return true;
}

/* Loads user virtual page UPAGE on the memory with frame
   allocation. */
bool
suppl_pt_load_page (void *upage)
{
  /* Get supplemental page table entry. */
  struct suppl_pte *pte = suppl_pt_get_page (upage);
  if (pte == NULL || pte->kpage != NULL)
    return false;

  /* Obtain a new frame. */
  void *kpage = frame_alloc (pte, PAL_USER);
  if (kpage == NULL)
    return false;

  /* Load page content for each page type. */
  bool writable = true;
  switch (pte->type)
    {
    /* Page filled with zeros. */
    case PAGE_ZERO:
      memset (kpage, 0, PGSIZE);
      break;

    /* Page content from the file system. */
    case PAGE_FILE:
      file_seek (pte->file, pte->ofs);
      if (file_read (pte->file, kpage, pte->read_bytes)
          != (int) pte->read_bytes)
        {
          frame_free (kpage);
          return false;
        }
      memset (kpage + pte->read_bytes, 0, pte->zero_bytes);
      writable = pte->writable;
      break;

    /* Page content from the swap disk. */
    case PAGE_SWAP:
      if (!swap_in (kpage, pte->swap_index))
        {
          frame_free (kpage);
          return false;
        };
      break;

    /* Unintended type. */
    default:
      NOT_REACHED ();
    }

  /* Install upage to kpage. */
  if (!pagedir_set_page (pte->pagedir, upage, kpage, writable))
    {
      frame_free (kpage);
      return false;
    }

  /* Set dirty value of kernel page to false. */
  pagedir_set_dirty (pte->pagedir, kpage, false);

  /* Append result to supplemental page table. */
  pte->kpage = kpage;

  return true;
}

/* Marks user virtual page UPAGE "not present" in page
   directory of the current process and removes correspoding
   supplemental page table element.
   This does not free or remove associated frame. */
void
suppl_pt_clear_page (void *upage)
{
  struct suppl_pte *pte = suppl_pt_get_page (upage);
  pagedir_clear_page (pte->pagedir, upage);
  suppl_pt_free_pte (pte);
}

/* Returns the supplemental page table entry associated with
   user virtual page UPAGE.
   Returns NULL if not exist. */
struct suppl_pte *
suppl_pt_get_page (void *upage)
{
  struct suppl_pt *pt = thread_current ()->suppl_pt;
  struct list_elem *e;
  for (e = list_begin (&pt->list); e != list_end (&pt->list);
       e = list_next (e))
    {
       struct suppl_pte *pte = list_entry (e, struct suppl_pte, elem); 
       if (pte->upage == upage)
         return pte;
    }
  return NULL;
}

/* Updates dirty bit at the given supplemental page table entry
   PTE to its associated page table entries and then returns it. */
bool
suppl_pt_update_dirty (struct suppl_pte *pte)
{
  ASSERT (pte != NULL);

  if (pte->kpage == NULL)
    return;

  pte->dirty = pte->dirty || pagedir_is_dirty (pte->pagedir, pte->upage)
               || pagedir_is_dirty (pte->pagedir, pte->kpage);

  return pte->dirty;
}

/* Frees a supplemental page table entry.
   This also removes the frame table entry, but not frees
   allocated page. */ 
static void
suppl_pt_free_pte (struct suppl_pte *pte)
{
  if (pte == NULL)
    return;

  if (pte->kpage != NULL)
    frame_remove (pte->kpage);
  else if (pte->type == PAGE_SWAP)
    swap_remove (pte->swap_index);
  list_remove (&pte->elem);
  free (pte);
}
