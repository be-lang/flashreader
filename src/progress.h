#ifndef FR_PROGRESS_H
#define FR_PROGRESS_H
#include <stdint.h>
#include <stddef.h>

uint64_t progress_hash(const void *data, size_t len);
int progress_save(uint64_t hash, int word_index);
int progress_load(uint64_t hash);  /* returns word_index or -1 */

#endif
