#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#define NUM_ADDR 15
#define IND_BLOCK 12
#define DIND_BLOCK 14
#define SIZE_BLOCK (DISK_SECTOR_SIZE / sizeof (disk_sector_t))

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    disk_sector_t sectors[NUM_ADDR];    /* Sectors. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[111];               /* Not used. */
  };

/* Inode indirect block. */
struct inode_indirect
  {
    disk_sector_t sectors[SIZE_BLOCK];  /* Sectors. */
  };

static bool inode_allocate (struct inode_disk *);
static bool inode_allocate_interval (struct inode_disk *, size_t);
static bool inode_allocate_at (disk_sector_t *, size_t);
static void inode_release (struct inode_disk *);
static void inode_release_interval (struct inode_disk *, size_t);
static void inode_release_at (disk_sector_t *, size_t);
static disk_sector_t inode_get_sector (const struct inode_disk *, off_t);
static bool inode_extend (struct inode_disk *, disk_sector_t, off_t length);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* Returns the minimum one among two sizes. */
static size_t
size_min (size_t s1, size_t s2)
{
  return s1 < s2 ? s1 : s2;
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode_get_sector (&inode->data, pos / DISK_SECTOR_SIZE);
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (inode_allocate (disk_inode))
        {
          buffer_cache_write (sector, disk_inode);
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  buffer_cache_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Write data into disk and remove buffer */
      buffer_cache_remove (inode->sector);

      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_release (&inode->data);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        /* Read full sector directly into caller's buffer. */
        buffer_cache_read (sector_idx, buffer + bytes_read);
      else 
        /* Read sector partially into caller's buffer. */
        buffer_cache_read_at (sector_idx, buffer + bytes_read, sector_ofs,
                              chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  if (!inode_extend (&inode->data, inode->sector, offset + size))
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        /* Write full sector directly to disk. */
        buffer_cache_write (sector_idx, buffer + bytes_written);
      else 
        {
          if (sector_ofs > 0 || chunk_size < sector_left) 
            buffer_cache_write_at (sector_idx, buffer + bytes_written,
                                   sector_ofs, chunk_size);
          else
            buffer_cache_write (sector_idx, buffer + bytes_written);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Adjusts number of sectors to allocate. */
static size_t
adjust_cnt (size_t alloc, size_t curr, size_t cnt)
{
  if (alloc + cnt < curr)
    return 0;
  if (alloc < curr)
    return alloc + cnt - curr;
  return cnt;
}

/* Allocates TARGET_SECTORS sectors to the inode DISK_INODE.
   This does not reallocate already allocated one.
   Returns TRUE if successful, FALSE otherwise. */
static bool
inode_allocate_interval (struct inode_disk *disk_inode, size_t target_sectors)
{
  size_t curr_sectors = bytes_to_sectors (disk_inode->length);
  if (curr_sectors >= target_sectors)
    return true;

  // Declare variables
  int i;
  disk_sector_t *pos;
  size_t cnt;
  size_t cnt_orig;
  size_t alloc_sectors = 0;

  // Allocate direct blocks
  cnt_orig = size_min (target_sectors, IND_BLOCK);
  cnt = adjust_cnt (alloc_sectors, curr_sectors, cnt_orig);
  pos = &disk_inode->sectors[cnt_orig - cnt];
  if (!inode_allocate_at (pos, cnt))
    return false;
  alloc_sectors += cnt_orig;

  // Done if all sectors are allocated
  if (alloc_sectors == target_sectors)
    return true;

  // Allocate indirect blocks
  for (i = 0; i < DIND_BLOCK - IND_BLOCK; i++)
    {
      // Allocate indirect block
      struct inode_indirect temp_ind_block;
      disk_sector_t *ind_pos = &disk_inode->sectors[IND_BLOCK + i];
      bool is_ind_created = false;
      if (curr_sectors <= alloc_sectors)
        {
          is_ind_created = true;
          if (!inode_allocate_at (ind_pos, 1))
            goto fail;
        }

      // Allocate sectors
      cnt_orig = size_min (target_sectors - alloc_sectors, SIZE_BLOCK);
      cnt = adjust_cnt (alloc_sectors, curr_sectors, cnt_orig);
      pos = &temp_ind_block.sectors[cnt_orig - cnt];
      if (!inode_allocate_at (pos, cnt))
        {
          if (is_ind_created)
            inode_release_at (ind_pos, 1);
          goto fail;
        }
      alloc_sectors += cnt_orig;

      // Wirte sector data
      if (cnt > 0)
        buffer_cache_write_at (*ind_pos,
                               &temp_ind_block.sectors[cnt_orig - cnt],
                               (cnt_orig - cnt) * sizeof (disk_sector_t),
                               cnt * sizeof (disk_sector_t));

      // Done if all sectors are allocated
      if (alloc_sectors == target_sectors)
        return true;
    }

  // Allocate doubly indirect blocks
  for (i = 0; i < NUM_ADDR - DIND_BLOCK; i++)
    {
      // Allocate doubly indirect block
      struct inode_indirect temp_dind_block;
      disk_sector_t *dind_pos = &disk_inode->sectors[DIND_BLOCK + i];
      bool is_dind_created = false;
      if (curr_sectors <= alloc_sectors)
        {
          is_dind_created = true;
          if (!inode_allocate_at (dind_pos, 1))
            goto fail;
        }

      // Allocate indirect blocks
      int j;
      for (j = 0; j < SIZE_BLOCK; j++)
        {
          // Allocate indirect block
          struct inode_indirect temp_ind_block;
          disk_sector_t *ind_pos = &temp_dind_block.sectors[i];
          bool is_ind_created = false;
          if (curr_sectors <= alloc_sectors)
            {
              is_ind_created = true;
              if (!inode_allocate_at (ind_pos, 1))
                {
                  if (j == 0 && is_dind_created)
                    inode_release_at (dind_pos, 1);
                  goto fail;
                }
            }

          // Allocate sectors
          cnt_orig = size_min (target_sectors - alloc_sectors, SIZE_BLOCK);
          cnt = adjust_cnt (alloc_sectors, curr_sectors, cnt_orig);
          pos = &temp_ind_block.sectors[cnt_orig - cnt];
          if (!inode_allocate_at (pos, cnt))
            {
              if (is_ind_created)
                {
                  if (j == 0 && is_dind_created)
                    inode_release_at (dind_pos, 1);
                  inode_release_at (ind_pos, 1);
                }
              goto fail;
            }
          alloc_sectors += cnt_orig;

          // Wirte sector data
          if (cnt > 0)
            {
              buffer_cache_write_at (*ind_pos,
                                     &temp_ind_block.sectors[cnt_orig - cnt],
                                     (cnt_orig - cnt) * sizeof (disk_sector_t),
                                     cnt * sizeof (disk_sector_t));
              if (is_ind_created)
                buffer_cache_write_at (*dind_pos, &temp_dind_block.sectors[j],
                                       j * sizeof (disk_sector_t),
                                       sizeof (disk_sector_t));
            }

          // Done if all sectors are allocated
          if (alloc_sectors == target_sectors)
            return true;
        }
    }

 fail:
  // Arrive here if allocation fails. Release allocated sectors.
  disk_inode->length = alloc_sectors * DISK_SECTOR_SIZE;
  inode_release_interval (disk_inode, curr_sectors);
  return false;
}

/* Allocates sectors to save data of size DISK_INODE->LENGTH.
   Returns TRUE if successful, FALSE otherwise. */
static bool
inode_allocate (struct inode_disk *disk_inode)
{
  off_t length = disk_inode->length;
  size_t sectors = bytes_to_sectors (length);
  disk_inode->length = 0;
  bool success = inode_allocate_interval (disk_inode, sectors);
  disk_inode->length = length;
  return success;
}

/* Allocates CNT sectors and saves their sector numbers at
   *SECTORP_ to *(SECTORP_ + CNT).
   Returns TRUE if successful, FALSE otherwise. */
static bool
inode_allocate_at (disk_sector_t *sectorp_, size_t cnt_)
{
  disk_sector_t *sectorp = sectorp_;
  size_t cnt = cnt_;

  size_t size = cnt;
  disk_sector_t temp_sector;
  static char zeros[DISK_SECTOR_SIZE];

  while (cnt > 0)
    {
      size = free_map_allocate_r (&cnt, size, &temp_sector);
      if (size == 0)
        {
          inode_release_at (sectorp_, cnt_ - cnt);
          return false;
        }

      size_t i;
      for (i = 0; i < size; i++)
        {
          *sectorp++ = temp_sector;
          buffer_cache_write (temp_sector++, zeros);
        }
    }

  return true;
}

/* Releases sectors controlled by DISK_INODE followed after
   CURR_SECTORS. */
static void
inode_release_interval (struct inode_disk *disk_inode, size_t curr_sectors)
{
  int i;
  disk_sector_t *pos;
  size_t cnt;
  size_t cnt_orig;
  size_t target_sectors = bytes_to_sectors (disk_inode->length);
  size_t alloc_sectors = 0;

  // Release direct blocks
  cnt_orig = size_min (target_sectors, IND_BLOCK);
  cnt = adjust_cnt (alloc_sectors, curr_sectors, cnt_orig);
  pos = &disk_inode->sectors[cnt_orig - cnt];
  inode_release_at (pos, cnt);
  alloc_sectors += cnt_orig;

  // Done if all sectors are released
  if (alloc_sectors == target_sectors)
    return;

  // Release indirect blocks
  for (i = 0; i < DIND_BLOCK - IND_BLOCK; i++)
    {
      // Read indirect block
      struct inode_indirect temp_ind_block;
      disk_sector_t *ind_pos = &disk_inode->sectors[IND_BLOCK + i];
      buffer_cache_read (*ind_pos, &temp_ind_block);

      // Release sectors
      cnt_orig = size_min (target_sectors - alloc_sectors, SIZE_BLOCK);
      cnt = adjust_cnt (alloc_sectors, curr_sectors, cnt_orig);
      pos = &temp_ind_block.sectors[cnt_orig - cnt];
      inode_release_at (pos, cnt);
      alloc_sectors += cnt_orig;

      // Release indirect block
      inode_release_at (ind_pos, 1);

      if (alloc_sectors == target_sectors)
        return;
    }

  // Release doubly indirect blocks
  for (i = 0; i < NUM_ADDR - DIND_BLOCK; i++)
    {
      // Read doubly indirect block
      struct inode_indirect temp_dind_block;
      disk_sector_t *dind_pos = &disk_inode->sectors[DIND_BLOCK + i];
      buffer_cache_read (*dind_pos, &temp_dind_block);

      // Release indirect blocks in doubly indirect block
      int j;
      for (j = 0; j < SIZE_BLOCK; j++)
        {
          // Read indirect block
          struct inode_indirect temp_ind_block;
          disk_sector_t *ind_pos = &temp_dind_block.sectors[i];
          buffer_cache_read (*ind_pos, &temp_ind_block);

          // Release sectors
          cnt_orig = size_min (target_sectors - alloc_sectors, SIZE_BLOCK);
          cnt = adjust_cnt (alloc_sectors, curr_sectors, cnt_orig);
          pos = &temp_ind_block.sectors[cnt_orig - cnt];
          inode_release_at (pos, cnt);
          alloc_sectors += cnt_orig;

          // Release indirect block
          inode_release_at (ind_pos, 1);

          if (alloc_sectors == target_sectors)
            {
              inode_release_at (dind_pos, 1);
              return;
            }
        }

      // Release doubly indirect block
      inode_release_at (dind_pos, 1);
    }
}

/* Releases sectors controlled by DISK_INODE. It does not
   release its own inode. */
static void
inode_release (struct inode_disk *disk_inode)
{
  inode_release_interval (disk_inode, 0);
}

/* Releases CNT sectors of numbers given by from *SECTORP to
   *(SECTORP + CNT - 1). It also sets corresponding disk_sector
   sector entry as 0. */
static void
inode_release_at (disk_sector_t *sectorp, size_t cnt)
{
  size_t i;
  for (i = 0; i < cnt; i++)
    {
      free_map_release (*sectorp, 1);
      *sectorp++ = 0;
    }
}

/* Returns the sector number of SECTOR_OFS-th sector of
   DISK_INODE. */
static disk_sector_t
inode_get_sector (const struct inode_disk *disk_inode, off_t sector_ofs)
{
  // Get from direct block
  if (sector_ofs < IND_BLOCK)
    return disk_inode->sectors[sector_ofs];
  sector_ofs -= IND_BLOCK;

  // Get from indirect block
  off_t ind_ofs = sector_ofs / SIZE_BLOCK;
  if (ind_ofs < DIND_BLOCK - IND_BLOCK)
    {
      struct inode_indirect temp_ind_block;
      disk_sector_t ind_pos = disk_inode->sectors[IND_BLOCK + ind_ofs];
      buffer_cache_read (ind_pos, &temp_ind_block);
      return temp_ind_block.sectors[sector_ofs % SIZE_BLOCK];
    }
  sector_ofs -= (DIND_BLOCK - IND_BLOCK) * SIZE_BLOCK;

  // Get from doubly indirect block
  off_t dind_ofs = sector_ofs / (SIZE_BLOCK * SIZE_BLOCK);
  struct inode_indirect temp_dind_block;
  disk_sector_t dind_pos = disk_inode->sectors[DIND_BLOCK + dind_ofs];
  buffer_cache_read (dind_pos, &temp_dind_block);
  sector_ofs = sector_ofs % (SIZE_BLOCK * SIZE_BLOCK);

  ind_ofs = sector_ofs / SIZE_BLOCK;
  struct inode_indirect temp_ind_block;
  disk_sector_t ind_pos = temp_dind_block.sectors[ind_ofs];
  buffer_cache_read (ind_pos, &temp_ind_block);
  return temp_ind_block.sectors[sector_ofs % SIZE_BLOCK];
}

/* Extends number of allocated sectors upto available number
   to store DISK_INODE->LENGTH data and then saves extended
   length by LENGTH. It also updates sector at SECTOR with
   updated DISK_INODE.
   Returns TRUE if successful, FALSE otherwise. */
static bool
inode_extend (struct inode_disk *disk_inode, disk_sector_t sector, off_t length)
{
  disk_sector_t sector_curr = bytes_to_sectors (disk_inode->length);
  disk_sector_t sector_max = bytes_to_sectors (length);

  // If no more sector is needed, just update length
  if (sector_max <= sector_curr)
    {
      if (disk_inode->length < length)
        {
          disk_inode->length = length;
          buffer_cache_write (sector, disk_inode);
        }
      return true;
    }

  // Allocate new sectors
  bool success = inode_allocate_interval (disk_inode, sector_max);
  if (success)
    {
      disk_inode->length = length;
      buffer_cache_write (sector, disk_inode);
    }
  return success;
}
