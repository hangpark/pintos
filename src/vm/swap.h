#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <inttypes.h>

void swap_table_init (void);
bool swap_in (void *kpage, size_t idx);
size_t swap_out (void *kpage);
void swap_remove (size_t idx);

#endif /* vm/swap.h */
