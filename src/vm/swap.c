#include <bitmap.h>
#include <debug.h>
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/swap.h"

#define SECTORS_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)

/* Swap table lock. */
static struct lock swap_table_lock;

/* Swap disk. */
static struct disk *swap_disk;

/* Swap table. */
static struct bitmap *swap_table;

/* Initializes the swap table. */
void
swap_table_init (void)
{
  lock_init (&swap_table_lock);

  /* Get swap disk. */
  swap_disk = disk_get (1, 1);
  if (swap_disk == NULL)
    {
      PANIC ("Cannot retrieve the swap disk");
      NOT_REACHED ();
    }

  /* Create swap table. */
  swap_table = bitmap_create (disk_size (swap_disk) / SECTORS_PER_PAGE);
  if (swap_table == NULL)
    {
      PANIC ("Cannot create the swap table");
      NOT_REACHED ();
    }

  /* Set all swap slots empty. */
  bitmap_set_all (swap_table, true);
}

/* Swaps the page at IDX of the swap disk into KPAGE.
   Returns true if successful, false otherwise. */
bool
swap_in (void *kpage, size_t idx)
{
  lock_acquire (&swap_table_lock);

  /* False if index is larger than swap table size. */
  if (idx >= bitmap_size (swap_table))
    {
      lock_release (&swap_table_lock);
      return false;
    }

  /* False if swap slot is empty. */
  if (bitmap_test (swap_table, idx))
    {
      lock_release (&swap_table_lock);
      return false;
    }

  /* Copy contents from swap disk to frame. */
  disk_sector_t sec_no = SECTORS_PER_PAGE * idx;
  size_t i;
  for (i = 0; i < SECTORS_PER_PAGE; i++)
    {
      disk_read (swap_disk, sec_no, kpage);
      sec_no++;
      kpage += DISK_SECTOR_SIZE;
    }

  /* Set swap slot empty. */
  bitmap_set (swap_table, idx, true);

  lock_release (&swap_table_lock);

  return true;
}

/* Swaps KPAGE out to the swap disk.
   Returns the index of swap slot if successful, BITMAP_ERROR
   if the swap table is full. */ 
size_t
swap_out (void *kpage)
{
  lock_acquire (&swap_table_lock);

  /* Get empty swap slot. */
  size_t idx = bitmap_scan (swap_table, 0, 1, true);
  if (idx == BITMAP_ERROR)
    {
      lock_release (&swap_table_lock);
      return BITMAP_ERROR;
    }

  /* Copy contents from frame to swap disk. */
  disk_sector_t sec_no = SECTORS_PER_PAGE * idx;
  size_t i;
  for (i = 0; i < SECTORS_PER_PAGE; i++)
    {
      disk_write (swap_disk, sec_no, kpage);
      sec_no++;
      kpage += DISK_SECTOR_SIZE;
    }

  /* Set swap slot not empty. */
  bitmap_set (swap_table, idx, false);

  lock_release (&swap_table_lock);

  return idx;
}

/* Marks IDX at the swap disk empty. */
void
swap_remove (size_t idx)
{
  lock_acquire (&swap_table_lock);
  bitmap_set (swap_table, idx, true);
  lock_release (&swap_table_lock);
}
