#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"

int is_mounted = 0;
int is_written = 0;

int mdadm_mount(void)
{

  if (is_mounted == 1)
  {
    return -1;
  }

  uint32_t op = JBOD_MOUNT;
  int chk_status = jbod_client_operation(op << 12, NULL);

  if (chk_status == 0)
  {
    is_mounted = 1;
    return 1;
  }

  return -1;
}

int mdadm_unmount(void)
{

  if (is_mounted == 0)
  {
    return -1;
  }

  uint32_t op = JBOD_UNMOUNT;
  int chk_status = jbod_client_operation(op << 12, NULL);

  if (chk_status == 0)
  {
    is_mounted = 0;
    return 1;
  }

  return -1;
}

int mdadm_write_permission(void)
{

  uint32_t op = JBOD_WRITE_PERMISSION;
  int write_status = jbod_client_operation(op << 12, NULL);

  if (write_status == 0)
  {
    is_written = 1; // Permission granted to write
    return 0;       // Success
  }

  return -1; // Failure
}

int mdadm_revoke_write_permission(void)
{

  uint32_t op = JBOD_REVOKE_WRITE_PERMISSION;
  int write_status = jbod_client_operation(op << 12, NULL);

  if (write_status == 0)
  {
    is_written = 0; // Permission revoked to write
    return 0;       // Success
  }

  return -1; // Failure
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf)
{

  // Check if mounted
  if (is_mounted == 0)
  {
    return -3;
  }

  // Check for valid length and buffer
  if (len == 0 && buf == NULL)
  {
    return len;
  }

  if (len != 0 && buf == NULL)
  {
    return -4;
  }

  // Check for valid address and read length
  if ((addr + len) > (JBOD_NUM_DISKS * JBOD_DISK_SIZE))
  {
    return -1;
  }

  if (len > 1024)
  {
    return -2;
  }

  uint32_t remaining_len = len;          // Number of bytes left to read
  uint32_t current_addr = addr;          // Current address to read from
  uint8_t buffer_array[JBOD_BLOCK_SIZE]; // Buffer to hold block data
  int bytes_read = 0;                    // Track total bytes read

  while (remaining_len > 0)
  {
    // Calculating the current disk, block, and position within block
    int current_Disk = current_addr / JBOD_DISK_SIZE;
    int current_Block = (current_addr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK;
    int current_PosInBlock = current_addr % JBOD_BLOCK_SIZE;

    // Check if cache is enabled and if the block is already in the cache
    uint8_t cache_buf[JBOD_BLOCK_SIZE];
    if (cache_enabled() && cache_lookup(current_Disk, current_Block, cache_buf) == 1)
    {
      // Cache hit
      int bytes_left_in_block = JBOD_BLOCK_SIZE - current_PosInBlock;
      int bytes_to_copy = (remaining_len < bytes_left_in_block) ? remaining_len : bytes_left_in_block;
      // Copy data from the cached block to the output buffer
      memcpy(buf + bytes_read, buffer_array + current_PosInBlock, bytes_to_copy);

      current_addr += bytes_to_copy;
      remaining_len -= bytes_to_copy;
      bytes_read += bytes_to_copy;
      continue;
    }

    // Seek to the correct disk
    uint32_t op_seek_disk = (JBOD_SEEK_TO_DISK << 12) | (current_Disk);
    if (jbod_client_operation(op_seek_disk, NULL) != 0)
    {
      return -1;
    }

    // Seek to the correct block
    uint32_t op_seek_block = (JBOD_SEEK_TO_BLOCK << 12) | (current_Block << 4);
    if (jbod_client_operation(op_seek_block, NULL) != 0)
    {
      return -1;
    }

    // Read block from disk to buffer
    if (jbod_client_operation(JBOD_READ_BLOCK << 12, buffer_array) != 0)
    {
      return -1;
    }

    // Insert block into cache
    if (cache_enabled())
    {
      cache_insert(current_Disk, current_Block, buffer_array);
    }

    // Calculate how much data to copy from block to output buffer
    int bytes_left_in_block = JBOD_BLOCK_SIZE - current_PosInBlock;
    int bytes_to_copy = (remaining_len < bytes_left_in_block) ? remaining_len : bytes_left_in_block;

    memcpy(buf + bytes_read, buffer_array + current_PosInBlock, bytes_to_copy);

    current_addr += bytes_to_copy;
    remaining_len -= bytes_to_copy;
    bytes_read += bytes_to_copy;
  }

  return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf)
{

  // Check if system is mounted
  // The first two if statements are the highest priority ones
  if (is_mounted == 0)
  {
    return -3;
  }

  // Check if write permission is granted
  if (is_written == 0)
  {
    return -5;
  }

  // Check for valid length and buffer

  if (len == 0 && buf == NULL)
  {
    return len;
  }
  if (len != 0 && buf == NULL)
  {
    return -4;
  }

  // Check for write length bounds
  if (len > 1024)
  {
    return -2;
  }

  // Check for address bounds
  if ((addr + len) > (JBOD_NUM_DISKS * JBOD_DISK_SIZE))
  {
    return -1;
  }

  uint32_t current_addr = addr; // Current address to write to
  uint8_t buffer_array[256];    // Buffer to hold block data
  int bytes_written = 0;        // Track the number of bytes written

  while (bytes_written < len)
  {
    // Calculate disk, block, and offset within the block
    int current_Disk = current_addr / JBOD_DISK_SIZE;
    int current_Block = (current_addr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK;
    int current_PosInBlock = (current_addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE;

    // Check if the block is already cached
    if (cache_enabled() && cache_lookup(current_Disk, current_Block, buffer_array) != 1)
    {

      // Cache miss: seek to the correct disk
      uint32_t op_seek_disk = (JBOD_SEEK_TO_DISK << 12) | current_Disk;
      if (jbod_client_operation(op_seek_disk, NULL) != 0)
      {
        return -1;
      }

      // Seek to the correct block
      uint32_t op_seek_block = (JBOD_SEEK_TO_BLOCK << 12) | (current_Block << 4);
      if (jbod_client_operation(op_seek_block, NULL) != 0)
      {
        return -1;
      }

      // Read the current block to avoid overwriting data outside the write range
      if (jbod_client_operation(JBOD_READ_BLOCK << 12, buffer_array) != 0)
      {
        return -1;
      }
    }

    int bytes_left_in_block = JBOD_BLOCK_SIZE - current_PosInBlock; // The number of bytes to write into
    bytes_left_in_block = bytes_left_in_block > len - bytes_written ? len - bytes_written : bytes_left_in_block;
    // Copy data from write_buf to buffer_array, starting at current_PosInBlock
    memcpy(buffer_array + current_PosInBlock, buf + bytes_written, bytes_left_in_block);

    // We need to seek to disk and block once again in order to adjust the pointer to where we need to start writing from again.
    uint32_t op_seek_disk = (JBOD_SEEK_TO_DISK << 12) | current_Disk;
    if (jbod_client_operation(op_seek_disk, NULL) != 0)
    {
      return -1;
    }

    // Seek to the correct block
    uint32_t op_seek_block = (JBOD_SEEK_TO_BLOCK << 12) | (current_Block << 4);
    if (jbod_client_operation(op_seek_block, NULL) != 0)
    {
      return -1;
    }

    if (jbod_client_operation(JBOD_WRITE_BLOCK << 12, buffer_array) != 0)
    {
      return -1;
    }

    if (cache_enabled())
    {
      cache_update(current_Disk, current_Block, buffer_array);
    }

    current_addr += bytes_left_in_block;  // Updating the addr pointer
    bytes_written += bytes_left_in_block; // Tracking the number of bytes written
  }

  return len;
}
