#include "ssp0_lab.h"
#include "lpc40xx.h"
#include <stdint.h>
void ssp0__init_lab(uint32_t max_clock_mhz) {
  // Refer to LPC User manual and setup the register bits correctly
  // a) Power on Peripheral
  LPC_SC->PCONP |= (1 << 21);
  LPC_SC->PCLKSEL = 1;
  // LPC_SSP2->CPSR = 4;
  LPC_IOCON->P0_15 &= ~(7 << 0);
  LPC_IOCON->P0_17 &= ~(7 << 0);
  LPC_IOCON->P0_18 &= ~(7 << 0);

  LPC_IOCON->P0_15 |= (2 << 0);
  LPC_IOCON->P0_17 |= (2 << 0);
  LPC_IOCON->P0_18 |= (2 << 0);
  // LPC_IOCON->P1_10 |= (0<<0);
  // b) Setup control registers CR0 and CR1
  LPC_SSP0->CR0 = (7 << 0);
  LPC_SSP0->CR1 = (1 << 1);
  // c) Setup prescalar register to be <= max_clock_mhz
  const uint32_t cpu_clock_Mhz = clock__get_core_clock_hz() / 1000000UL;
  uint8_t ssp0_div = 2;
  while (max_clock_mhz <= (cpu_clock_Mhz / ssp0_div) && ssp0_div <= 254) {
    ssp0_div += 2;
  }
  LPC_SSP0->CPSR = ssp0_div;
}

uint8_t ssp0__exchange_byte_lab(uint8_t data_out) {
  // Configure the Data register(DR) to send and receive data by checking the status register
  LPC_SSP0->DR = data_out;
  while (LPC_SSP0->SR & (1 << 4))
    ;
  return (LPC_SSP0->DR & 0xFF);
}
