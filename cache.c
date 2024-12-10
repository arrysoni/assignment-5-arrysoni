#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

// Uncomment the below code before implementing cache functioncs.
static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries)
{
  // num_enteries minimum at 2
  if (num_entries < 2)
  {
    return -1;
  }

  // num_enteries maximum at 4096
  if (num_entries > 4096)
  {

    return -1;
  }

  // Cache can never be NULL
  if (cache != NULL)
  {
    return -1;
  }

  // Dynamically allocate space for num_entries cache entries
  cache = (cache_entry_t *)malloc(num_entries * sizeof(cache_entry_t));

  if (cache == NULL)
  {
    return -1;
  }

  cache_size = num_entries;

  // All contents may contain garbage value as the memory is allocated with malloc
  // Initializing the values
  for (int i = 0; i < num_entries; i++)
  {
    cache[i].valid = false;
    cache[i].disk_num = -1;
    cache[i].block_num = -1;
    cache[i].clock_accesses = 0;
  }

  clock = 0; // Reset the clock
  num_queries = 0;
  num_hits = 0;

  return 1;
}

int cache_destroy(void)
{
  if (cache == NULL)
  {
    return -1;
  }

  free(cache); // Freeing up the dynamically allocated space

  cache = NULL;
  cache_size = 0;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf)
{
  // buf can never be NULL
  if (buf == NULL)
  {
    return -1;
  }

  if (cache == NULL)
  {
    return -1;
  }

  num_queries++; // Keep track of the lookup attempts

  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      // Block found in the cache
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_hits++; // Keep track of the lookup successes
      num_queries++;
      clock++;
      cache[i].clock_accesses = clock; // Entry was accessed recently
      return 1;
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf)
{
  if (buf == NULL)
  {
    return;
  }

  if (cache == NULL)
  {
    return;
  }

  for (int i = 0; i < cache_size; i++)
  {

    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      // Entry exists in cache, so we update it
      if (cache[i].disk_num == disk_num && cache[i].block_num == block_num)
      {
        memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
        clock++;
        cache[i].clock_accesses = clock; // Entry was accessed recently
        return;
      }
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf)
{
  if (buf == NULL)
  {
    return -1;
  }

  if (cache == NULL)
  {
    return -1;
  }

  // Validate the disk and block numbers
  if (disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK)
  {
    return -1;
  }

  // If the block exists in the cache, we update it with the current elements
  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      return -1;
    }
  }

  // Looking up for an empty spot
  for (int i = 0; i < cache_size; i++)
  {
    if (!cache[i].valid)
    {
      cache[i].valid = true;
      cache[i].disk_num = disk_num;
      cache[i].block_num = block_num;
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].clock_accesses = clock++;
      return 1;
    }
  }

  // Evict the Most Recently Used (MRU) entry when cache is full
  int mru_index = 0;
  for (int i = 1; i < cache_size; i++)
  {
    if (cache[i].clock_accesses > cache[mru_index].clock_accesses)
    {
      mru_index = i; // Index of the MRU entry
    }
  }

  // Initialize new content at mru_index
  cache[mru_index].valid = true;
  cache[mru_index].disk_num = disk_num;
  cache[mru_index].block_num = block_num;
  memcpy(cache[mru_index].block, buf, JBOD_BLOCK_SIZE);
  cache[mru_index].clock_accesses = clock++;

  return 1;
}

bool cache_enabled(void)
{
  if (cache != NULL && cache_size > 0)
  {
    return true;
  }
  return false;
}

void cache_print_hit_rate(void)
{
  fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float)num_hits / num_queries);
}

int cache_resize(int new_num_entries)
{
  if (new_num_entries < 2 || new_num_entries > 4096)
  {
    return -1;
  }

  if (cache == NULL)
  {
    return -1;
  }

  // Allocate memory for the new cache with the specified size
  cache_entry_t *new_cache = (cache_entry_t *)malloc(new_num_entries * sizeof(cache_entry_t));
  if (new_cache == NULL)
  {
    return -1; // Memory allocation failed
  }

  // If the new size is smaller, only copy up to the new size, else copy old enteries
  int entries_to_copy = (new_num_entries < cache_size) ? new_num_entries : cache_size;
  for (int i = 0; i < entries_to_copy; i++)
  {
    if (cache[i].valid)
    {
      new_cache[i] = cache[i];
    }
  }

  // Initialize cache_enteries that have not been copied from old cache
  for (int i = 0; i < new_num_entries; i++)
  {
    new_cache[i].valid = false;
    new_cache[i].disk_num = -1;
    new_cache[i].block_num = -1;
    new_cache[i].clock_accesses = 0;
  }

  free(cache);

  cache = new_cache;            // Global cache pointer points to new cache
  cache_size = new_num_entries; // Global cache_size is the new cache size

  return 1;
}
