#ifndef _BOARDS_LUFTFUGL_RP2040_H
#define _BOARDS_LUFTFUGL_RP2040_H

#define PICO_DEFAULT_UART             0
#define PICO_DEFAULT_UART_TX_PIN      0
#define PICO_DEFAULT_UART_RX_PIN      1
#define PICO_FLASH_SPI_CLKDIV         2
#define PICO_FLASH_SIZE_BYTES         (4 * 1024 * 1024)   // confirmed via SFDP, see agent.md §1
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#define PICO_RP2040_B0_SUPPORTED      0

#endif
