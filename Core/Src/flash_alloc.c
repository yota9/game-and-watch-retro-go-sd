#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "porting.h"
#include "gw_flash.h"
#include "gw_linker.h"

// Use the largest BLOCK size among:
// 256 bytes - SPI flash page size
// 512 bytes - SD card block size
// 1024 bytes - SPI SRAM page size
#define BLOCK_LENGTH 1024UL

#if EXTFLASH_FORCE_SRAM == 0

#define FLASH_MAGIC 0x46534C53UL
#define FLASH_VERSION 1
#define TOTAL_BLOCKS 126
#define ALIGN_BOUNDARY 4096UL

// Align the store block size to 4096 bytes
#define STORE_BLOCK_SIZE (((__SPI_FLASH_SIZE__ - ALIGN_BOUNDARY) / \
                           TOTAL_BLOCKS) & (~(ALIGN_BOUNDARY - 1)))

static_assert(STORE_BLOCK_SIZE >= ALIGN_BOUNDARY,
              "Store block size is too small");

struct flash_entry {
    uint32_t tag;   /* Unique data tag */
    uint16_t block; /* Allocation start block number */
    uint16_t count; /* Count of blocks */
};

struct flash_entries {
    uint32_t magic;
    uint16_t version;
    uint16_t num_entries;
    uint16_t next_free_index;
    struct flash_entry entry[TOTAL_BLOCKS];
};

// Just a check to make sure the flash entries fits in 1KB,
// adjust TOTAL_BLOCKS if new fields in struct flash_entries are added
static_assert(sizeof(struct flash_entries) <= 1024,
              "Flash entries size should be <= 1024");

static struct flash_entries ram_entries[1];

// Store the flash entries at the end of the flash memory

static inline uint32_t get_flash_entries_off(void)
{
    return __SPI_FLASH_SIZE__ - ALIGN_BOUNDARY;
}

static inline const struct flash_entries *get_flash_entries(void)
{
    return (struct flash_entries *)(get_flash_entries_off() +
                                    __SPI_FLASH_BASE__);
}

static void ram_alloc_init(void)
{
    ram_entries->magic = FLASH_MAGIC;
    ram_entries->version = FLASH_VERSION;
    ram_entries->num_entries = 1;
    ram_entries->next_free_index = 0;

    ram_entries->entry[0].block = 0;
    ram_entries->entry[0].count = TOTAL_BLOCKS;
    ram_entries->entry[0].tag = 0;
}

static void flash_alloc_init(void)
{
    static bool flash_alloc_initialized = false;
    if (flash_alloc_initialized)
        return;

    if (!FlashCtx.Presented) {
        printf("Unable to load data, no flash installed\n");
        abort();
    }

    const struct flash_entries *flash = get_flash_entries();
    if (flash->magic == FLASH_MAGIC && flash->version == FLASH_VERSION) {
        memcpy(ram_entries, flash, sizeof(*ram_entries));
        flash_alloc_initialized = true;
        return;
    }

    printf("Flash allocator is not initialized, initializing\n");
    ram_alloc_init();
    flash_alloc_initialized = true;
}

static void store_flash_entries(void)
{
    wdog_refresh();
    FlashCtx.DisableMemoryMappedMode();
    FlashCtx.Erase(get_flash_entries_off(), ALIGN_BOUNDARY);
    FlashCtx.Write(get_flash_entries_off(), ram_entries, sizeof(*ram_entries));
    FlashCtx.EnableMemoryMappedMode();
}

static struct flash_entry *is_loaded(uint32_t blocks, uint32_t tag)
{
    for (uint32_t i = 0; i < ram_entries->num_entries; i++) {
        if (ram_entries->entry[i].tag == tag &&
            ram_entries->entry[i].count == blocks)
            return &ram_entries->entry[i];
    }

    return NULL;
}

static uint32_t free_flash_round_robin(uint32_t blocks_to_free) {
    struct flash_entries *fe = ram_entries;
    uint16_t index_to_free = fe->next_free_index % fe->num_entries;

    // If there is not enough space till end to free, reset the free index
    if (fe->entry[index_to_free].block + blocks_to_free > TOTAL_BLOCKS)
        fe->next_free_index = 0;

    while (fe->num_entries > 0) {
        index_to_free = fe->next_free_index % fe->num_entries;
        fe->next_free_index = index_to_free + 1;

        // Mark the entry as free
        fe->entry[index_to_free].tag = 0;

        // Combine with previous free block if possible
        // by moving the index to the left and further combining
        // with next (current) free block
        if (index_to_free > 0 && fe->entry[index_to_free - 1].tag == 0) {
            --index_to_free;
            --fe->next_free_index;
        }

        // Combine with next free block if possible
        if (index_to_free + 1 < fe->num_entries && fe->entry[index_to_free + 1].tag == 0) {
            fe->entry[index_to_free].count += fe->entry[index_to_free + 1].count;
            for (uint32_t j = index_to_free + 1; j < fe->num_entries - 1; j++)
                fe->entry[j] = fe->entry[j + 1];

            fe->num_entries--;
        }

        if (fe->entry[index_to_free].count >= blocks_to_free)
            break;
    }

    assert(fe->num_entries && "Unexpected zero num_entries\n");
    return index_to_free;
}

static struct flash_entry *allocate_flash(uint32_t blocks_needed, uint32_t tag) {
    struct flash_entries *fe = ram_entries;
    struct flash_entry *entry;
    uint32_t idx = fe->num_entries - 1;

    if (blocks_needed > TOTAL_BLOCKS) {
        printf("Flash is too small to load!\n");
        abort();
    }

    // Check if there is a free unallocated block that fits the size
    if (fe->entry[idx].tag == 0 && fe->entry[idx].count >= blocks_needed) {
        entry = &fe->entry[idx];
    } else {
        idx = free_flash_round_robin(blocks_needed);
        entry = &fe->entry[idx];
    }

    assert(entry->count >= blocks_needed &&
           "Unexpected block size less then blocks needed\n");

    // Split the allocated block if it's too big
    if (entry->count > blocks_needed) {
        for (uint32_t i = fe->num_entries - 1; i > idx; i--)
            fe->entry[i + 1] = fe->entry[i];

        fe->entry[idx + 1].block = entry->block + blocks_needed;
        fe->entry[idx + 1].count = entry->count - blocks_needed;
        fe->entry[idx + 1].tag = 0;
        fe->num_entries++;
    }

    entry->count = blocks_needed;
    entry->tag = tag;

    // Update flash with new entries
    store_flash_entries();
    return entry;
}

static uint32_t get_tag(uint32_t sd_address, uint32_t size, uint8_t *ram_buffer)
{
    const uint32_t len = size < BLOCK_LENGTH ? size : BLOCK_LENGTH;
    uint32_t crc = crc32_le(0, (uint8_t *)&sd_address, sizeof(sd_address));
    crc = crc32_le(crc, (uint8_t *)&size, sizeof(size));
    SdCtx.Read(sd_address, ram_buffer, len);
    crc = crc32_le(crc, ram_buffer, len);
    return crc;
}

void reset_flash_allocator(void)
{
    if (!FlashCtx.Presented)
        return;

    ram_alloc_init();
    store_flash_entries();
}

uint32_t copy_sd_to_flash(uint32_t sd_address, uint32_t size)
{
    int64_t copy_left = size;
    struct flash_entry *entry;
    uint32_t flash_addr;
    uint8_t ram_buffer[BLOCK_LENGTH];

    flash_alloc_init();

    // Round up to the nearest block size
    const uint32_t blocks_needed = (size + STORE_BLOCK_SIZE - 1) / STORE_BLOCK_SIZE;
    const uint32_t tag = get_tag(sd_address, size, ram_buffer);
    entry = is_loaded(blocks_needed, tag);
    if (entry) {
        printf("Data is already loaded in flash\n");
        return (__SPI_FLASH_BASE__ + entry->block * STORE_BLOCK_SIZE);
    }

    entry = allocate_flash(blocks_needed, tag);

    flash_addr = entry->block * STORE_BLOCK_SIZE;
    FlashCtx.DisableMemoryMappedMode();
    while (copy_left > 0) {
        if (flash_addr % ALIGN_BOUNDARY == 0)
            FlashCtx.Erase(flash_addr, ALIGN_BOUNDARY);

        SdCtx.Read(sd_address, ram_buffer, BLOCK_LENGTH);
        FlashCtx.Write(flash_addr, ram_buffer, BLOCK_LENGTH);
        sd_address += BLOCK_LENGTH;
        flash_addr += BLOCK_LENGTH;
        copy_left -= BLOCK_LENGTH;
    }

    FlashCtx.EnableMemoryMappedMode();
    return (__SPI_FLASH_BASE__ + entry->block * STORE_BLOCK_SIZE);
}

#else

void reset_flash_allocator(void)
{
    return;
}

uint32_t copy_sd_to_flash(uint32_t sd_address, uint32_t size)
{
    int64_t copy_left = size;
    uint32_t flash_addr = 0;
    uint8_t ram_buffer[BLOCK_LENGTH];

    FlashCtx.DisableMemoryMappedMode();
    while (copy_left > 0) {
        SdCtx.Read(sd_address, ram_buffer, BLOCK_LENGTH);
        FlashCtx.Write(flash_addr, ram_buffer, BLOCK_LENGTH);
        sd_address += BLOCK_LENGTH;
        flash_addr += BLOCK_LENGTH;
        copy_left -= BLOCK_LENGTH;
    }

    FlashCtx.EnableMemoryMappedMode();
    return __SPI_FLASH_BASE__;
}

#endif // !EXTFLASH_FORCE_SRAM
