#include "filesys/cache.h"
#include <debug.h>
#include <list.h>
#include <stdbool.h>
#include <string.h>
#include "devices/timer.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define BUFFER_CACHE_NUM 64
#define THREAD_FLUSH_BACK "buffer-cache-flush-back"
#define THREAD_READ_AHEAD "buffer-cache-read-ahead"
#define FLUSH_BACK_INTERVAL 500

/* Buffer cache entry. */
struct buffer_cache_entry
  {
    bool usebit;                      /* Whether in use or not. */
    disk_sector_t sector;             /* Sector number. */
    bool dirty;                       /* Dirty bit. */
    bool accessed;                    /* Accesed bit. */
    uint8_t data[DISK_SECTOR_SIZE];   /* Data. */
  };

/* Buffer cache lock. */
static struct lock buffer_cache_lock;

/* Buffer cache entries. */
static struct buffer_cache_entry buffer_cache[BUFFER_CACHE_NUM];

/* Buffer cache array traversing position. */
static int buffer_cache_pos;

/* Read-ahead entry. */
struct read_ahead_entry
  {
    disk_sector_t sector;             /* Sector number. */
    struct list_elem elem;            /* List element. */
  };

/* Read-ahead semaphore. */
static struct semaphore read_ahead_sema;

/* Read-ahead lock. */
static struct lock read_ahead_lock;

/* Read-ahead list. */
static struct list read_ahead_list;

static struct buffer_cache_entry *buffer_cache_find (disk_sector_t);
static struct buffer_cache_entry *buffer_cache_get_empty (void);
static struct buffer_cache_entry *buffer_cache_to_evict (void);
static struct buffer_cache_entry *buffer_cache_fetch (disk_sector_t, bool read);

/* Thread function to flush back to the disk periodically. */
static void
buffer_cache_thread_flush_back (void *aux UNUSED)
{
  for (;;)
    {
      timer_sleep (FLUSH_BACK_INTERVAL);
      buffer_cache_done ();
    }
}

/* Thread function to read ahead sectors from the disk. */
static void
buffer_cache_thread_read_ahead (void *aux UNUSED)
{
  for (;;)
    {
      sema_down (&read_ahead_sema);
      lock_acquire (&read_ahead_lock);

      struct list_elem *e = list_pop_front (&read_ahead_list);
      struct read_ahead_entry *entry = list_entry (e, struct read_ahead_entry,
                                                   elem);
      lock_acquire (&buffer_cache_lock);
      buffer_cache_fetch (entry->sector, true);
      lock_release (&buffer_cache_lock);

      lock_release (&read_ahead_lock);
    }
}

/* Initializes the buffer cache. */
void
buffer_cache_init (void)
{
  lock_init (&buffer_cache_lock);
  buffer_cache_pos = 0;
  sema_init (&read_ahead_sema, 0);
  lock_init (&read_ahead_lock);
  list_init (&read_ahead_list);
  int i;
  for (i = 0; i < BUFFER_CACHE_NUM; i++)
    buffer_cache[i].usebit = false;
  thread_create (THREAD_FLUSH_BACK, PRI_MAX, buffer_cache_thread_flush_back,
                 NULL);
  thread_create (THREAD_READ_AHEAD, PRI_MAX, buffer_cache_thread_read_ahead,
                 NULL);
}

/* Shuts down the buffer cache module, writing any unwritten data
   to disk. */
void
buffer_cache_done (void)
{
  lock_acquire (&buffer_cache_lock);

  int i;
  for (i = 0; i < BUFFER_CACHE_NUM; i++)
    {
      struct buffer_cache_entry *entry = buffer_cache + i;
      if (entry->usebit && entry->dirty)
        disk_write (filesys_disk, entry->sector, entry->data);
    }

  lock_release (&buffer_cache_lock);
}

/* Reads the SECTOR of filesys disk into ADDR.
   If the sector is cached, reads data from it. Otherwise,
   caches it and reads. This method may include evicting
   less accessed cached sector to the disk if any cache entry
   is available. */
void
buffer_cache_read (disk_sector_t sector, void *addr)
{
  buffer_cache_memcpy (sector, addr, 0, DISK_SECTOR_SIZE);
}

/* Writes the data from ADDR to the SECTOR.
   If the sector is cached, writes data to it. Otherwise,
   caches it and writes. This method may include evicting
   less accessed cached sector to the disk if any cache entry
   is available. */
void
buffer_cache_write (disk_sector_t sector, const void *addr)
{
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *entry = buffer_cache_fetch (sector, false);
  memcpy (entry->data, addr, DISK_SECTOR_SIZE);
  entry->accessed = true;
  entry->dirty = true;

  lock_release (&buffer_cache_lock);
}

/* Copies the part of data of SECTOR to ADDR.
   If the sector is cached, copies data from it. Otherwise,
   caches it and copies. This method may include evicting
   less accessed cached sector to the disk if any cach entry
   is available. */
void
buffer_cache_memcpy (disk_sector_t sector, void *addr, off_t offset,
                     size_t size)
{
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *entry = buffer_cache_fetch (sector, true);
  memcpy (addr, entry->data + offset, size);
  entry->accessed = true;

  lock_release (&buffer_cache_lock);
}

/* Removes a buffer cache entry of the given SECTOR if exists. */
void
buffer_cache_remove (disk_sector_t sector)
{
  lock_acquire (&buffer_cache_lock);

  struct buffer_cache_entry *entry = buffer_cache_find (sector);
  if (entry != NULL && entry->dirty)
    {
      disk_write (filesys_disk, entry->sector, entry->data);
      entry->usebit = false;
    }

  lock_release (&buffer_cache_lock);
}

/* Read-ahead the given SECTOR into the buffer cache. */
void
buffer_cache_read_ahead (disk_sector_t sector)
{
  struct read_ahead_entry *entry = malloc (sizeof (struct read_ahead_entry));
  if (entry == NULL)
    return;
  entry->sector = sector;

  lock_acquire (&read_ahead_lock);
  list_push_back (&read_ahead_list, &entry->elem);
  lock_release (&read_ahead_lock);
}

/* Returns the buffer cache entry of the given SECTOR. Returns
   NULL if does not exist. */
static struct buffer_cache_entry *
buffer_cache_find (disk_sector_t sector)
{
  int i;
  for (i = 0; i < BUFFER_CACHE_NUM; i++)
    {
      if (buffer_cache[i].usebit && buffer_cache[i].sector == sector)
        return buffer_cache + i;
    }
  return NULL;
}

/* Returns an empty buffer cache entry. Returns NULL if every
   entries are in use. */
static struct buffer_cache_entry *
buffer_cache_get_empty (void)
{
  int i;
  for (i = 0; i < BUFFER_CACHE_NUM; i++)
    {
      if (!buffer_cache[i].usebit)
        return buffer_cache + i;
    }
  return NULL;
}

/* Returns a buffer cache entry to evict.
   This method uses the clock replacement algorithm. */
static struct buffer_cache_entry *
buffer_cache_to_evict (void)
{
  struct buffer_cache_entry *entry;
  for (;;)
    {
      entry = buffer_cache + buffer_cache_pos;
      if (!entry->accessed)
        break;
      entry->accessed = false;
    }
  buffer_cache_pos = (buffer_cache_pos + 1) % BUFFER_CACHE_NUM;
  return entry;
}

/* Returns the buffer cache entry of the given SECTOR.
   If the entry does not exist, it fetches the entry for
   the sector. If READ is set by TRUE, then it also reads
   the data from the disk. */
static struct buffer_cache_entry *
buffer_cache_fetch (disk_sector_t sector, bool read)
{
  struct buffer_cache_entry *entry = buffer_cache_find (sector);
  if (entry == NULL)
    {
      entry = buffer_cache_get_empty ();
      if (entry == NULL)
        {
          entry = buffer_cache_to_evict ();
          if (entry->dirty)
            disk_write (filesys_disk, entry->sector, entry->data);
        }
      else
        entry->usebit = true;

      if (read)
        {
          disk_read (filesys_disk, sector, entry->data);
          entry->dirty = false;
        }
      entry->sector = sector;
    }

  return entry;
}
