#include "softspi.h"
#include "main.h"

__attribute__((always_inline))
static inline void gpio_pause() {
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
    __asm("NOP");
}

static void delay_us(uint32_t usec) {
    uint32_t cycles_per_us = SystemCoreClock / 1000000;
    uint32_t nop_count = cycles_per_us * (usec / 2);

    while(nop_count--) {
        __asm("NOP");
    }
}

void SoftSpi_WriteRead(SoftSPI *spi, uint8_t *txData, uint8_t *rxData, uint32_t len) {
    int i, j;
    uint8_t txBit, rxBit;
    uint8_t txByte, rxByte;

    HAL_GPIO_WritePin(spi->sck.port, spi->sck.pin, GPIO_PIN_RESET);
    if (spi->cs.port)
        HAL_GPIO_WritePin(spi->cs.port, spi->cs.pin, GPIO_PIN_RESET);

    for (i = 0; i < len; i++) {
        txByte = txData[i];
        rxByte = 0;

        for (j = 7; j >= 0; j--) {
            txBit = (txByte & (1 << j)) ? 1 : 0;

            HAL_GPIO_WritePin(spi->mosi.port, spi->mosi.pin, txBit ? GPIO_PIN_SET : GPIO_PIN_RESET);
            gpio_pause();
            HAL_GPIO_WritePin(spi->sck.port, spi->sck.pin, GPIO_PIN_SET);
            delay_us(spi->DelayUs);

            rxBit = HAL_GPIO_ReadPin(spi->miso.port, spi->miso.pin) == GPIO_PIN_SET ? 1 : 0;
            rxByte <<= 1;
            rxByte |= rxBit;

            HAL_GPIO_WritePin(spi->sck.port, spi->sck.pin, GPIO_PIN_RESET);
            delay_us(spi->DelayUs);
        }

        wdog_refresh();
        if (rxData)
            rxData[i] = rxByte;
    }

     if (spi->cs.port)
        HAL_GPIO_WritePin(spi->cs.port, spi->cs.pin, GPIO_PIN_SET);
}
