#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gw_flash.h"
#include "softspi.h"
#include "main.h"
#include "utils.h"

// NOTE Use switch_ospi_gpio() in every exported function that uses sd card
// and swtich back to ospi before exit. This is needed since flash memory
// is memory-mmaped and should always be ready to be used, so it is more
// prioritised.

#define DBG(...) printf(__VA_ARGS__)

#ifndef MIN
#define MIN(a,b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a < _b ? _a : _b; })
#endif // !MIN

#define BLOCK_SIZE 512ULL

#include "gw_linker.h"
static const uint32_t SD_BASE_ADDRESS = (uint32_t)&__EXTFLASH_START__;

static struct {
    SoftSPI spi[1];
    bool isSdV2 : 1;
    bool ccs : 1;
} sd = {
    .spi[0] = {
        .sck = { .port = GPIO_FLASH_CLK_GPIO_Port, .pin = GPIO_FLASH_CLK_Pin },
        .mosi = { .port = GPIO_FLASH_MOSI_GPIO_Port, .pin = GPIO_FLASH_MOSI_Pin },
        .miso = { .port = GPIO_FLASH_MISO_GPIO_Port, .pin = GPIO_FLASH_MISO_Pin },
        .cs = { .port = GPIO_FLASH_NCS_GPIO_Port, .pin = GPIO_FLASH_NCS_Pin },
        .DelayUs = 20,
        .csIsInverted = true
    }
};

// =============================================================================
// SD card responses
// =============================================================================

#define START_BLOCK_TOKEN 0xFE

typedef bool (*response_fn)(uint8_t *r);

#define R1_IDLE 0ULL
#define R1_ERASE_RESET 1ULL
#define R1_ILLEGAL_COMMAND 2ULL
#define R1_CRC_ERROR 3ULL
#define R1_ERASE_SEQUENCE_ERROR 4ULL
#define R1_ADDRESS_ERROR 5ULL
#define R1_PARAMETER_ERROR 6ULL
#define R1_ALWAYS_ZERO 7ULL

static bool responseR1(uint8_t *r) {
    *r = 0xFF;
    for (int i = 0; i < 10 && *r == 0xFF; ++i)
        SoftSpi_WriteDummyRead(sd.spi, r, sizeof(*r));

    return *r != 0xFF;
}

#define R2_CARD_LOCKED 0ULL
#define R2_WP_ERASE_SKIP 1ULL
#define R2_ERROR 2ULL
#define R2_CC_ERROR 3ULL
#define R2_CARD_ECC_FAILED 4ULL
#define R2_WP_VIOLATION 5ULL
#define R2_ERASE_PARAM 6ULL
#define R2_OUT_OF_RANGE 7ULL

#define R2_GET_R1(r) (((uint8_t *)r)[1])

__attribute__((unused))
static bool responseR2(uint8_t *r) {
    if (!responseR1((uint8_t *)&(R2_GET_R1(r))))
        return false;

    SoftSpi_WriteDummyRead(sd.spi, &r[0], sizeof(*r));
    return !r[1] || r[1] == (1 << R1_IDLE);
}

#define R3_V27_28 15ULL
#define R3_V28_29 16ULL
#define R3_V29_30 17ULL
#define R3_V30_31 18ULL
#define R3_V31_32 19ULL
#define R3_V32_33 20ULL
#define R3_V33_34 21ULL
#define R3_V34_35 22ULL
#define R3_V35_36 23ULL
#define R3_V18 24ULL
#define R3_UHS2 29ULL
#define R3_CCS 30ULL
#define R3_READY 31ULL

#define R3R7_GET_R1(r) (((uint8_t *)r)[4])

static bool responseR3R7(uint8_t *r) {
    if (!responseR1(&(R3R7_GET_R1(r))))
        return false;

    SoftSpi_WriteDummyRead(sd.spi, &r[3], sizeof(*r));
    SoftSpi_WriteDummyRead(sd.spi, &r[2], sizeof(*r));
    SoftSpi_WriteDummyRead(sd.spi, &r[1], sizeof(*r));
    SoftSpi_WriteDummyRead(sd.spi, &r[0], sizeof(*r));
    return !r[4] || r[4] == (1 << R1_IDLE);
}

static bool responseCMD8(uint8_t *r) {
    if (responseR3R7(r))
        return true;

    // Old v1 sd card, fault is expected
    if (r[4] & (1 << R1_ILLEGAL_COMMAND))
        return true;

    return false;
}

struct response {
    uint64_t r0;
};

// =============================================================================
// SD card commands
// =============================================================================

#define SD_GO_IDLE_STATE_CMD 0
#define SD_SEND_OP_COND_CMD 1
#define SD_SEND_INTERFACE_COND_CMD 8
#define SD_READ_SINGLE_BLOCK_CMD 17
#define SD_WRITE_SINGLE_BLOCK_CMD 24
#define SD_SEND_OP_COND_ACMD 41
#define SD_APP_CMD 55
#define SD_READ_OCR_CMD 58

enum cmd_list {
    GO_IDLE_STATE = 0,
    SEND_OP_COND,
    SEND_INTERFACE_COND,
    READ_SINGLE_BLOCK,
    WRITE_SINGLE_BLOCK,
    SEND_OP_COND_ACMD,
    APP_CMD,
    READ_OCR,
};

static struct sd_cmd {
    uint8_t cmd;
    uint8_t crc;
    response_fn response;
} sd_cmds[] = {
    [GO_IDLE_STATE] = { SD_GO_IDLE_STATE_CMD, 0x95, responseR1 },
    [SEND_OP_COND] = { SD_SEND_OP_COND_CMD, 0x0, responseR1 },
    [SEND_INTERFACE_COND] = { SD_SEND_INTERFACE_COND_CMD, 0x86, responseCMD8 },
    [READ_SINGLE_BLOCK] = { SD_READ_SINGLE_BLOCK_CMD, 0x0, responseR1 },
    [WRITE_SINGLE_BLOCK] = { SD_WRITE_SINGLE_BLOCK_CMD, 0x0, responseR1 },
    [SEND_OP_COND_ACMD] = { SD_SEND_OP_COND_ACMD, 0x0, responseR1 },
    [APP_CMD] = { SD_APP_CMD, 0x0, responseR1 },
    [READ_OCR] = { SD_READ_OCR_CMD, 0x0, responseR3R7 },
};

// =============================================================================

static void EnableMemoryMappedMode(void) {}
static void DisableMemoryMappedMode(void) {}
static void ReadSR(uint8_t dest[1]) { *dest = 0; }
static void ReadCR(uint8_t dest[1]) { *dest = 0; }
static const char* GetName(void) { return "Sd"; }
static uint32_t GetSmallestEraseSize(void) { return 1; }
void Format (void) {}
void Erase(uint32_t address, uint32_t size) {}
static void ReadId(uint8_t dest[3]) {
    dest[0] = 0;
    dest[1] = 0;
    dest[2] = 0;
}

static void __send_cmd_payload(uint8_t cmd, uint32_t arg, uint32_t crc) {
    uint8_t spi_cmd_payload[6] = {cmd | 0x40, arg >> 24, arg >> 16, arg >> 8, arg, crc | 0x1};
    SoftSpi_WriteDummyRead(sd.spi, NULL, 2);
    SoftSpi_WriteRead(sd.spi, spi_cmd_payload, NULL, sizeof(spi_cmd_payload));
    wdog_refresh();
}

static bool __send_cmd(enum cmd_list cmd, uint32_t arg, struct response *response) {
    __send_cmd_payload(sd_cmds[cmd].cmd, arg, sd_cmds[cmd].crc);
    return sd_cmds[cmd].response((uint8_t *)response);
}

static struct response send_cmd(enum cmd_list cmd, uint32_t arg) {
    struct response response = {};
    for (int i = 0; i < 255; i++) {
        if (__send_cmd(cmd, arg, &response))
            return response;
    }

    printf("SD: Failed to send cmd %d\n", cmd);
    abort();
}

static void send_read_cmd(uint32_t addr) {
    uint8_t ret;

    addr -= SD_BASE_ADDRESS;
    assert((addr & (BLOCK_SIZE - 1)) == 0 && "Address is not aligned");
    if (sd.ccs)
        addr /= BLOCK_SIZE;

    if (send_cmd(READ_SINGLE_BLOCK, addr).r0) {
        printf("SD: Failed to send read cmd\n");
        abort();
    }

    // We would fail on watchdog if somthing is wrong here
    do {
        SoftSpi_WriteDummyRead(sd.spi, &ret, 1);
    } while(ret != START_BLOCK_TOKEN);
}

static void finish_read_cmd(void) {
    // Skip checksum reading
    SoftSpi_WriteDummyRead(sd.spi, NULL, 2);
}

static void send_write_cmd(uint32_t addr) {
    struct response response;
    const uint8_t start_block_token = START_BLOCK_TOKEN;

    assert((addr & (BLOCK_SIZE - 1)) == 0 && "Address is not aligned");
    if (sd.ccs)
        addr /= BLOCK_SIZE;

    // We would fail on watchdog if something is wrong here
    do {
        response = send_cmd(WRITE_SINGLE_BLOCK, addr);
    } while (response.r0);

    // Send dummy pre-send byte and start block token
    SoftSpi_WriteDummyRead(sd.spi, NULL, 1);
    SoftSpi_WriteRead(sd.spi, &start_block_token, NULL, 1);
}

static void finish_write_cmd(void) {
    uint8_t rbyte;

    // Dummy crc
    SoftSpi_WriteDummyRead(sd.spi, NULL, 2);

    // We would fail on watchdog if something is wrong here
    // Read status byte
    do {
        SoftSpi_WriteDummyRead(sd.spi, &rbyte, 1);
    } while(rbyte == 0xFF);

    if ((rbyte & 0xF) != 0x05) {
        printf("SD: Failed to write data block\n");
        abort();
    }

    // Wait for data to be written
    do {
        SoftSpi_WriteDummyRead(sd.spi, &rbyte, 1);
    } while (rbyte == 0x00);
}

static void sd_card_read_write(uint32_t address, void *pbuffer, size_t buffer_size, bool is_read) {
    uint8_t *buffer = (uint8_t *)pbuffer;
    const uint32_t start_address = address & ~(BLOCK_SIZE - 1);

    if (!buffer_size)
        return;

    switch_ospi_gpio(false);

    // Read/write first unaligned block
    if (address != start_address) {
        if (is_read)
            send_read_cmd(start_address);
        else
            send_write_cmd(start_address);

        // Skip bytes before target address
        SoftSpi_WriteDummyRead(sd.spi, NULL, address - start_address);

        // Read/write buffer size or remaining bytes in block
        const uint32_t next_block = start_address + BLOCK_SIZE;
        const uint32_t bytes_to_rw = MIN(buffer_size, next_block - address);
        if (is_read)
            SoftSpi_WriteDummyRead(sd.spi, buffer, bytes_to_rw);
        else
            SoftSpi_WriteRead(sd.spi, buffer, NULL, bytes_to_rw);

        buffer += bytes_to_rw;
        buffer_size -= bytes_to_rw;
        address += bytes_to_rw;

        // Skip remaining bytes if buffer_size is 0
        SoftSpi_WriteDummyRead(sd.spi, NULL, next_block - address);
        address = next_block;

        if (is_read)
            finish_read_cmd();
        else
            finish_write_cmd();
    }

    // Read/write data in blocks
    while (buffer_size >= BLOCK_SIZE) {
        if (is_read) {
            send_read_cmd(address);
            SoftSpi_WriteDummyRead(sd.spi, buffer, BLOCK_SIZE);
            finish_read_cmd();
        } else {
            send_write_cmd(address);
            SoftSpi_WriteRead(sd.spi, buffer, NULL, BLOCK_SIZE);
            finish_write_cmd();
        }

        address += BLOCK_SIZE;
        buffer += BLOCK_SIZE;
        buffer_size -= BLOCK_SIZE;
    }

    // Read/write remaining bytes
    if (buffer_size) {
        const uint32_t next_block = address + BLOCK_SIZE;

        if (is_read) {
            send_read_cmd(address);
            SoftSpi_WriteDummyRead(sd.spi, buffer, buffer_size);
        } else {
            send_write_cmd(address);
            SoftSpi_WriteRead(sd.spi, buffer, NULL, buffer_size);
        }

        address += buffer_size;

        // Skip remaining bytes
        SoftSpi_WriteDummyRead(sd.spi, NULL, next_block - address);

        if (is_read)
            finish_read_cmd();
        else
            finish_write_cmd();
    }

    switch_ospi_gpio(true);
}

static void sd_card_read(uint32_t address, void *pbuffer, size_t buffer_size)
{
    sd_card_read_write(address, pbuffer, buffer_size, true);
}

static void sd_card_write(uint32_t address, const void *pbuffer, size_t buffer_size)
{
    sd_card_read_write(address, (void *)pbuffer, buffer_size, false);
}

static void Init(OSPI_HandleTypeDef *hospi) {
    struct response response;
    int i;

    switch_ospi_gpio(false);

    SoftSpi_WriteDummyReadCsLow(sd.spi, NULL, 10);

    response = send_cmd(GO_IDLE_STATE, 0);
    if (response.r0 != (1 << R1_IDLE)) {
        printf("SD: Go idle state failed\n");
        abort();
    }

    // 3.3V + AA pattern
    response = send_cmd(SEND_INTERFACE_COND, 0x1AA);
    sd.isSdV2 = !(R3R7_GET_R1(&response) == (1 << R1_ILLEGAL_COMMAND));

    // Needed by manual
    send_cmd(READ_OCR, 0);

    for (i = 0; i < 255; i++) {
        if (sd.isSdV2) {
            response = send_cmd(APP_CMD, 0);
            if (response.r0 && response.r0 != (1 << R1_IDLE))
                continue;

            // High capacity card support
            response = send_cmd(SEND_OP_COND_ACMD, 0x40000000);
            if (!response.r0)
                break;
        } else {
            response = send_cmd(SEND_OP_COND, 0);
            if (!response.r0)
                break;
        }
    }

    if (i == 255) {
        printf("SD: Failed to initialize\n");
        abort();
    }

    if (sd.isSdV2) {
        response = send_cmd(READ_OCR, 0);
        if (!(response.r0 & (1 << R3_READY))) {
            printf("SD: Not ready\n");
            abort();
        }

        sd.ccs = response.r0 & (1 << R3_CCS);
    }

    sd.spi->DelayUs = 0;

    switch_ospi_gpio(true);
}

struct FlashCtx SdCtx = {
    .Init = Init,
    .Write = sd_card_write,
    .Read = sd_card_read,
    .EnableMemoryMappedMode = EnableMemoryMappedMode,
    .DisableMemoryMappedMode = DisableMemoryMappedMode,
    .Format = Format,
    .Erase = Erase,
    .ReadId = ReadId,
    .ReadSR = ReadSR,
    .ReadCR = ReadCR,
    .GetSmallestEraseSize = GetSmallestEraseSize,
    .GetName = GetName,
    .Presented = true
};
