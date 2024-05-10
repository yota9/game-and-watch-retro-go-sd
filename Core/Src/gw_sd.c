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

#define DBG(...) printf(__VA_ARGS__)

#define GO_IDLE_STATE 0x0
#define SEND_OP_COND 0x1

#define RESPONSE_OK 0x0
#define RESPONSE_IN_IDLE_STATE 0x1

static struct {
    SoftSPI spi[1];
} sd = {
    .spi[0] = {
        .sck = { .port = GPIO_OSPI_CLK_GPIO_Port, .pin = GPIO_OSPI_CLK_Pin },
        .mosi = { .port = GPIO_OSPI_MOSI_GPIO_Port, .pin = GPIO_OSPI_MOSI_Pin },
        .miso = { .port = GPIO_OSPI_MISO_GPIO_Port, .pin = GPIO_OSPI_MISO_Pin },
        .cs = { .port = NULL, .pin = 0 },
        .DelayUs = 20
    }
};

static uint8_t __send_cmd(uint8_t cmd, uint32_t arg) {
    const uint8_t crc = 0x95; // Only used for GO_IDLE_STATE cmd
    uint8_t spi_cmd_payload[6] = {cmd | 0x40, arg >> 24, arg >> 16, arg >> 8, arg, crc};
    uint64_t tmp = ~0;

    SoftSpi_WriteRead(sd.spi, spi_cmd_payload, NULL, sizeof(spi_cmd_payload));
    SoftSpi_WriteRead(sd.spi, (uint8_t *)&tmp, (uint8_t *)&tmp, sizeof(tmp));

    for (uint8_t i = 0; i < 8; ++i)
        if (((uint8_t *)&tmp)[i] != 0xFF)
            return ((uint8_t *)&tmp)[i];

    return 0xFF;
}

static void send_cmd(uint8_t cmd, uint32_t arg, uint8_t response) {
    for (int i = 0; i < 255; i++) {
        if (__send_cmd(cmd, arg) == response)
            return;
    }

    printf("SD: Failed to send cmd %d\n", cmd);
    abort();
}

void OSPI_EnableMemoryMappedMode(void) {}
void OSPI_DisableMemoryMappedMode(void) {}
void OSPI_ReadSR(uint8_t dest[1]) { *dest = 0; }
void OSPI_ReadCR(uint8_t dest[1]) { *dest = 0; }
const char* OSPI_GetFlashName(void) { return "sd"; }
uint32_t OSPI_GetSmallestEraseSize(void) { return 1; }
void OSPI_NOR_WriteEnable(void) {}
void OSPI_ReadJedecId(uint8_t dest[3]) {
    dest[0] = 0;
    dest[1] = 0;
    dest[2] = 0;
}

void OSPI_ChipErase(void) {

}

bool OSPI_Erase(uint32_t *address, uint32_t *size) {
    return true;
}

void OSPI_EraseSync(uint32_t address, uint32_t size) {

}

void OSPI_PageProgram(uint32_t address, const uint8_t *buffer, size_t buffer_size) {

}


void OSPI_Program(uint32_t address, const uint8_t *buffer, size_t buffer_size) {

}

void OSPI_Init(OSPI_HandleTypeDef *hospi) {
    uint8_t tmp = 0xFF;
    for (int i = 0; i < 10; i++)
        SoftSpi_WriteRead(sd.spi, &tmp, NULL, 1);

    sd.spi->cs.port = GPIO_OSPI_NCS_GPIO_Port;
    sd.spi->cs.pin = GPIO_OSPI_NCS_Pin;

    send_cmd(GO_IDLE_STATE, /* arg */ 0, RESPONSE_IN_IDLE_STATE);
    send_cmd(SEND_OP_COND, /* arg */ 0, RESPONSE_OK);

    sd.spi->DelayUs = 2; // TODO probably set 0?
}
