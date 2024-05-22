#ifndef _GW_FLASH_H_
#define _GW_FLASH_H_

#include "stm32h7xx_hal.h"

#include <stdint.h>
#include <stdbool.h>

struct FlashCtx {
    void (*Init)(OSPI_HandleTypeDef *hospi);
    void (*Write)(uint32_t address, const uint8_t *buffer, size_t buffer_size);
    void (*Read)(uint32_t address, void *buffer, size_t buffer_size);
    void (*EnableMemoryMappedMode)(void);
    void (*DisableMemoryMappedMode)(void);
    void (*Format)(void);
    void (*Erase)(uint32_t address, uint32_t size);
    void (*ReadId)(uint8_t dest[3]);
    void (*ReadSR)(uint8_t dest[1]);
    void (*ReadCR)(uint8_t dest[1]);
    uint32_t (*GetSmallestEraseSize)(void);
    const char* (*GetName)(void);
    bool Presented : 1;
};

extern struct FlashCtx FlashCtx;
extern struct FlashCtx SdCtx;

__attribute__((always_inline))
static inline struct FlashCtx *get_flash_ctx(void) {
#if SD_CARD == 0
    return &FlashCtx;
#else
    return &SdCtx;
#endif // !SD_CARD
}

#endif
