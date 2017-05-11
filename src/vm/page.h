#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "filesys/file.h"
#include "vm/swap.h"

/* Page statuses. */
enum page_type
  {
    PAGE_ZERO,          /* Page with zero fill. */
    PAGE_FILE,          /* Page content from the file system. */
    PAGE_SWAP           /* Page content from the swap disk. */
  };

/* Supplemental page table. */
struct suppl_pt
  {
    struct list list;   /* List. */
  };

/* Supplemental page table entry. */
struct suppl_pte
  {
    enum page_type type;            /* Page type. */ 
    void *upage;                    /* User virtual page. */
    void *kpage;                    /* Kernel virtual page.
                                       NULL if not on the memory. */
    uint32_t *pagedir;              /* Page directory. */
    bool dirty;                     /* Dirty bit. */
    union
      {
        struct                      /* Only for page type PAGE_FILE. */
          {
            struct file *file;      /* File that contains contents. */
            off_t ofs;              /* File offset. */
            uint32_t read_bytes;    /* Read bytes. */
            uint32_t zero_bytes;    /* Zero bytes. */
            bool writable;          /* Writable flag. */
          };
        struct                      /* Only for page type PAGE_SWAP. */
          {
            size_t swap_index;      /* Swap disk index. */
          };
      };

    struct list_elem elem;          /* List element. */
  };

struct suppl_pt *suppl_pt_create (void);
void suppl_pt_destroy (struct suppl_pt *);

bool suppl_pt_set_zero (void *upage);
bool suppl_pt_set_file (void *upage, struct file *, off_t, uint32_t read_bytes,
                        uint32_t zero_bytes, bool writable);
bool suppl_pt_load_page (void *upage);
struct suppl_pte *suppl_pt_get_page (void *upage);
void suppl_pt_clear_page (void *upage);

bool suppl_pt_update_dirty (struct suppl_pte *);

#endif /* vm/page.h */
