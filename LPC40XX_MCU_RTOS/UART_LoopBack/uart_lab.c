#include "uart_lab.h"
/***
 * * Pin Layout for UART 2 (Available)
 * * U2 - RX - P0.11 / P2.9
 * * U2 - TX - P0.10 / P2.8
 * * *****************************
 * * Pin Layout for UART 3 (Available)
 * * U3 - RX - P0.0 / P4.28 / P0.25
 * * U3 - TX - P0.1 / P4.29 / P0.26
 * */

/*	uart_lab_intialization
 *	a) Check LSR for Receive Data Ready
 *  b) Copy data from RBR register to input_byte
 * 	c)if the UARTn receiver FIFO is not empty, copy the data from BRB register.
 * 	d)Set up the GPIO configuration
 * 		UART2 RX->P2.8
 * 		UART2 TX->P2.9
 */

void uart_lab__init(uart_number_e uart, uint32_t peripheral_clock, uint32_t baud_rate) {
  switch (uart) {
  case UART__2:
    // a) Power on Peripheral
    // Bit 24 in LPC_SC->PCONP - UART 2 power/clock control bit.
    LPC_SC->PCONP |= (1 << 24);
    // d) Set up pin configuration
    LPC_IOCON->P2_8 |= (2 << 0);
    LPC_IOCON->P2_9 |= (2 << 0);
    break;
  case UART__3:
    // a) Power on Peripheral
    // Bit 25 in LPC_SC->PCONP - UART 2 power/clock control bit.
    LPC_SC->PCONP |= (1 << 25);
    // d) Set up pin configuration
    LPC_IOCON->P0_0 |= (2 << 0);
    LPC_IOCON->P0_1 |= (2 << 0);
    break;
  }
  // b) Setup DLL, DLM, FDR, LCR registers
  // First, we need to set DLAB to 1 before we can modify
  // the UART Divisor value
  // Bit 7 - DLAB (if set) Enable access to Divisor Latches.
  uarts_number[uart]->LCR = (1 << 7);
  // Calculate the UART Divisor value and save the result in DLM and
  // DLL register
  uint16_t div = (peripheral_clock) / (16 * baud_rate);
  uarts_number[uart]->DLM = (div >> 8);
  uarts_number[uart]->DLL = (div & 0xFF);
  // Make sure bit 4 of FDR is set to 1
  uarts_number[uart]->FDR = (1 << 4);
  // After modifying the prescalar value, we need to clear Bit 7 of DLAB
  // => Disable access to Divisor Latches
  uarts_number[uart]->LCR &= ~(1 << 7);
  // Select 8 data length for UART
  uarts_number[uart]->LCR |= (0x3 << 0);
  // Refer to LPC User manual and setup the register bits correctly
}

/*	UART receive
 *	a) Check LSR for Receive Data Ready
 *  b) Copy data from RBR register to input_byte
 * if the UARTn receiver FIFO is not empty, copy the data from BRB register.
 */

/* uart_lab__polled_get
 * 	- convenience function
 * 		(a) Check LSR for Receive Data Ready
 * 		(b) Get the character from the UAR Driver
 * 		(copy data from THR register)
 *
 * @uart:
 * @*input_byte: character copy from THR
 * 		-check the LSR register is empty or not
 * 		-copy the data to the THR reisetr to send data
 * statement:
 * 		@LSR empty:wait
 * 		@LSR non empty:copy the data from register
 */

bool uart_lab__polled_get(uart_number_e uart, char *input_byte) {
  while (((uarts_number[uart]->LSR) & 0x1) == 0) {
  }
  if (((uarts_number[uart]->LSR) & 0x1) == 1) {
    *input_byte = uarts_number[uart]->RBR;
    while (((uarts_number[uart]->LSR) & 0x1) == 0) {
    }
    return true;
  }
  return false;
}
/*
 * uart_lab_pooled_put
 * - convenience function
 *	(a) Check LSR for Receive Data Ready
 *	(b) To send the character from the UART Driver
 *		(copy data to THR register)
 *
 * @uart: the uart port(e.g: UART__2,UART__3)
 * @output_byte:character send
 * 		-check the LSR register is empty or not
 * 		-copy the data to the THR reisetr to send data
 * statement:
 * 		@LSR empty:wait
 * 		@LSR non empty:copy the data to register
 */

bool uart_lab__polled_put(uart_number_e uart, char output_byte) {
  // a) Check LSR for Transmit Hold Register Empty
  // b) Copy output_byte to THR register
  while (((uarts_number[uart]->LSR >> 5) & 0x1) == 0) {
  }
  if ((uarts_number[uart]->LSR >> 5) & 0x1) {
    uarts_number[uart]->THR = output_byte;
    while (((uarts_number[uart]->LSR >> 5) & 0x1) == 0) {
    }
    return true;
  }
  return false;
}
