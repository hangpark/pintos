#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stddef.h>
#include "devices/disk.h"
#include "filesys/off_t.h"

void buffer_cache_init (void);
void buffer_cache_done (void);
void buffer_cache_read (disk_sector_t, void *);
void buffer_cache_read_at (disk_sector_t, void *, off_t, size_t);
void buffer_cache_write (disk_sector_t, const void *);
void buffer_cache_write_at (disk_sector_t, const void *, off_t, size_t);
void buffer_cache_remove (disk_sector_t);
void buffer_cache_read_ahead (disk_sector_t);

#endif /* filesys/cache.h */
