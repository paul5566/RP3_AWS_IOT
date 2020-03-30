#include "uart_lab.h"
#include "FreeRTOS.h"
#include "lpc40xx.h"
#include "lpc_peripherals.h"
#include "queue.h"

void uart_lab__init(uart_number_e uart, uint32_t peripheral_clock, uint32_t baud_rate) {
  // Refer to LPC User manual and setup the register bits correctly
  // a) Power on Peripheral
  const uint8_t power_uart2 = 24;
  const uint8_t power_uart3 = 25;
  const uint8_t DLAB_bit = (1 << 7);
  const uint32_t divadd_val = (0xF << 0);
  const uint32_t mul_val = (1 << 4);
  const uint8_t stop_bits = (1 << 2);
  const uint8_t eight_bit_data = (0x3 << 0);
  uint16_t DIV = (uint16_t)((peripheral_clock / (16 * baud_rate)));
  if (uart == UART__2) {
    LPC_SC->PCONP |= (1 << power_uart2);
    // b) Setup DLL, DLM, FDR, LCR registers
    LPC_UART2->LCR |= DLAB_bit; // (set) DLAB = 1
    LPC_UART2->DLL |= (DIV >> 0) && 0xFF;
    LPC_UART2->DLM |= (DIV >> 8) && 0xFF;
    LPC_UART2->FDR &= ~divadd_val;
    LPC_UART2->FDR |= mul_val;
    LPC_UART2->LCR &= ~DLAB_bit; // (disable) DLAB = 0
    // Enable 2 stop bits
    LPC_UART2->LCR |= (1 << 2);
    // Enable 8-bit transfer
    LPC_UART2->LCR |= eight_bit_data;
  } else if (uart == UART__3) {
    LPC_SC->PCONP |= (1 << power_uart3);
    // b) Setup DLL, DLM, FDR, LCR registers
    LPC_UART2->FDR &= ~divadd_val;
    LPC_UART3->FDR |= mul_val;
    LPC_UART3->LCR |= DLAB_bit; // (set) DLAB = 1
    LPC_UART3->DLM |= (DIV >> 8) && 0xFF;
    LPC_UART3->DLL |= (DIV >> 0) && 0xFF;
    LPC_UART3->LCR &= ~(DLAB_bit); // (disable) DLAB = 0
    // Enable 2 stop bits
    LPC_UART3->LCR |= stop_bits;
    // Enable 8-bit transfer
    LPC_UART3->LCR |= eight_bit_data;
  }
}

bool uart_lab__polled_get(uart_number_e uart, char *input_byte) {
  // a) Check LSR for Receive Data Ready
  // b) Copy data from RBR register to input_byte
  if (uart == UART__2) {
    if (LPC_UART2->LSR & (1 << 0)) {
      *input_byte = LPC_UART2->RBR;
    }
  } else if (uart == UART__3) {
    if (LPC_UART3->LSR & (1 << 0)) {
      *input_byte = LPC_UART3->RBR;
    }
  }
}

bool uart_lab__polled_put(uart_number_e uart, char output_byte) {
  // a) Check LSR for Transmit Hold Register Empty
  // b) Copy output_byte to THR register
  if (uart == UART__2) {
    if (LPC_UART2->LSR & (1 << 5)) {
      LPC_UART2->THR = output_byte;
    }
  } else if (uart == UART__3) {
    if (LPC_UART3->LSR & (1 << 5)) {
      LPC_UART3->THR = output_byte;
    }
  }
}

static QueueHandle_t your_uart_rx_queue;

// Private function of our uart_lab.c
static void your_receive_interrupt(void) {
  // TODO: Read the IIR register to figure out why you got interrupted
  uint8_t rda_intr_id = (0x2 << 1);
  uint8_t data_ready = (1 << 0);
  const char byte;
  char *byte_p = &byte;
  // TODO: Based on IIR status, read the LSR register to confirm if there is data to be read
  if (LPC_UART2->IIR & rda_intr_id) {
    if (LPC_UART2->LSR & data_ready) {
      *byte_p = LPC_UART2->RBR;
    }
  } else if (LPC_UART3->IIR & rda_intr_id) {
    if (LPC_UART3->LSR & data_ready) {
      *byte_p = LPC_UART3->RBR;
    }
  }
  // TODO: Based on LSR status, read the RBR register and input the data to the RX Queue
  xQueueSendFromISR(your_uart_rx_queue, &byte, NULL);
}

// Public function to enable UART interrupt
// TODO Declare this at the header file
void uart__enable_receive_interrupt(uart_number_e uart_number) {
  // TODO: Use lpc_peripherals.h to attach your interrupt
  // TODO: Enable interrupt by reading the LPC User manual
  // Hint: Read about the IER register
  if (uart_number == UART__2) {
    lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__UART2, your_receive_interrupt);
    LPC_UART2->IER |= (1 << 0);
  } else if (uart_number == UART__3) {
    lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__UART3, your_receive_interrupt);
    LPC_UART3->IER |= (1 << 0);
  }
  // TODO: Create your RX queue
  your_uart_rx_queue = xQueueCreate(20, sizeof(uint32_t));
}

// Public function to get a char from the queue (this function should work without modification)
// TODO: Declare this at the header file
bool uart_lab__get_char_from_queue(char *byte, uint32_t timeout) {
  return xQueueReceive(your_uart_rx_queue, byte, timeout);
}