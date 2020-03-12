#include "lpc40xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef enum {
  UART__2 = 0,
  UART__3 = 1,
} uart_number_e;

static LPC_UART_TypeDef *uarts_number[2] = {LPC_UART2, LPC_UART3};

void uart_lab__init(uart_number_e uart, uint32_t peripheral_clock, uint32_t baud_rate);

bool uart_lab__polled_get(uart_number_e uart, char *input_byte);

bool uart_lab__polled_put(uart_number_e uart, char output_byte);
