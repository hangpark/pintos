#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* Free map file. */
static struct bitmap *free_map;      /* Free map, one bit per disk sector. */

/* Initializes the free map. */
void
free_map_init (void) 
{
  free_map = bitmap_create (disk_size (filesys_disk));
  if (free_map == NULL)
    PANIC ("bitmap creation failed--disk is too large");
  bitmap_mark (free_map, FREE_MAP_SECTOR);
  bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

/* Allocates CNT consecutive sectors from the free map and stores
   the first into *SECTORP.
   Returns true if successful, false if all sectors were
   available. */
bool
free_map_allocate (size_t cnt, disk_sector_t *sectorp) 
{
  disk_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false);
  if (sector != BITMAP_ERROR
      && free_map_file != NULL
      && !bitmap_write (free_map, free_map_file))
    {
      bitmap_set_multiple (free_map, sector, cnt, false); 
      sector = BITMAP_ERROR;
    }
  if (sector != BITMAP_ERROR)
    *sectorp = sector;
  return sector != BITMAP_ERROR;
}

/* Makes CNT sectors starting at SECTOR available for use. */
void
free_map_release (disk_sector_t sector, size_t cnt)
{
  ASSERT (bitmap_all (free_map, sector, cnt));
  bitmap_set_multiple (free_map, sector, cnt, false);
  bitmap_write (free_map, free_map_file);
}

/* Opens the free map file and reads it from disk. */
void
free_map_open (void) 
{
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_read (free_map, free_map_file))
    PANIC ("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void
free_map_close (void) 
{
  file_close (free_map_file);
}

/* Creates a new free map file on disk and writes the free map to
   it. */
void
free_map_create (void) 
{
  /* Create inode. */
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map)))
    PANIC ("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_write (free_map, free_map_file))
    PANIC ("can't write free map");
}

/* Allocates consecutive sectors from the free map. This can be
   used for recursive jobs.

   Desired number of sectors to allocate from now is set by *CNTP.
   But, kernel may not be able to allocate *CNTP consecutive sectors.
   Then it decreases SIZE by half until (decreased) SIZE consecutive
   sectors becomes available to be allocated.

   After allocating n consecutive sectors, this method decrease
   *CNTP by n so that desired number of sector reduced which can be
   allocated in the next loop.

   This returns the number of consecutive allocated sectors.

   Example usage:

   size_t cnt = 100; // Number of sectors to allocate.
   size_t size = cnt;

   while (cnt > 0)
    {
      size = free_map_allocate_r (&cnt, size, &sector);
      if (size == 0)
        goto fail; // Cannot allocate.

      // Do whatever with size and sector.
    }
*/
size_t
free_map_allocate_r (size_t *cntp, size_t size, disk_sector_t *sectorp)
{
  ASSERT (cntp != NULL);
  ASSERT (sectorp != NULL);

  if (size > *cntp)
    size = *cntp;

  while (size > 0)
    {
      if (free_map_allocate (size, sectorp))
        break;
      size = size >> 1;
    }

  *cntp -= size;

  return size;
}
