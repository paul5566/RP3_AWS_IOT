#include "FreeRTOS.h"
#include "board_io.h"
#include "delay.h"
#include "gpio.h"
#include "task.h"
#include "uart_lab.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * uart_read_task
 * - convenience task to poll the character from the UART
 *   Driver(copy data from RBR register)
 *
 * @uart_lab_polled:copy the data from the RBR reisetr to
 * 					byte_read
 * statement:
 * 		@sucessfully:keep pirnting while register (LSR & 0x1 == 0)
 */

void uart_read_task(void *p) {

  char byte_read;

  fprintf(stderr, "UART2 RX receive\n");
  while (1) {
    /*
                TODO:
            Use uart_lab__polled_get() function and print the received
    */
    if (uart_lab__polled_get(UART__3, &byte_read)) {
      fprintf(stderr, "%c", byte_read);
      vTaskDelay(100);
    }
  }
}
/*
 * uart_read_task
 * - convenience task to send the character from the UART
 *   Driver(copy data to THR register)
 *
 * @uart_lab_polled_put:copy the data to the THR reisetr to send data
 *
 * statement:
 * 		@sucessfully:keep sending while the string is done
 */

void uart_write_task(void *p) {
  char send_out_string[14] = "Hello World!\n";
  while (1) {
    // TODO: Use uart_lab__polled_put() function and send a value
    for (int i = 0; i < sizeof(send_out_string) / sizeof(send_out_string[0]); i++) {
      uart_lab__polled_put(UART__3, send_out_string[i]);
      vTaskDelay(100);
    }
  }
}
int main(void) {
  uart_lab__init(UART__3, 96000000, 115200); // Intialize UART2 or UART3 (your choice)
  xTaskCreate(uart_write_task, "UART_Write_Task", 2048U / sizeof(void *), NULL, PRIORITY_MEDIUM, NULL);
  xTaskCreate(uart_read_task, "UART_Read_Task", 20248U / sizeof(void *), NULL, PRIORITY_LOW, NULL);
  vTaskStartScheduler();
}
