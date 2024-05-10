#pragma once

#include <stdint.h>

#include <stm32h7xx_hal.h>

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} SoftSPI_Pin;

typedef struct {
    SoftSPI_Pin sck;
    SoftSPI_Pin mosi;
    SoftSPI_Pin miso;
    SoftSPI_Pin cs;
    uint32_t DelayUs;
} SoftSPI;

void SoftSpi_WriteRead(SoftSPI *spi, uint8_t *txData, uint8_t *rxData, uint32_t len);
