#include "FreeRTOS.h"
#include "delay.h"
#include "ff.h"
#include "gpio.h"
#include "lpc_peripherals.h"
#include "semphr.h"
#include "sj2_cli.h"
#include "stdio.h"
#include "string.h"
#include "task.h"
#include "uart.h"

TaskHandle_t mp3_task;
TaskHandle_t mp3_sdcard;
TaskHandle_t mp3_control;

char list[50][13];
int32_t count = 0;
int32_t current = 0;
uint32_t decodeTime = 0;

uint8_t songptr[512];
uint8_t songptr2[64];
uint8_t songPos;
long mp3Size;
long lsize;
bool sdready = true;
uint8_t mp3Sel = 0;

char spi2_ExchangeByte(char out) {
  LPC_SSP1->DR = out;
  while (LPC_SSP1->SR & (1 << 4))
    ;
  return (char)LPC_SSP1->DR;
}

void mp3Flush() {
  for (int b = 0; b < 2500; b++) {
    LPC_GPIO0->CLR = (1 << 0);
    spi2_ExchangeByte(0x00);
    LPC_GPIO0->SET = (1 << 0);
  }
}

void mp3SIWrite(uint8_t addr, uint8_t payload1, uint8_t payload2) {
  LPC_GPIO0->CLR = (1 << 22); // Select xCS
  delay__ms(2);
  spi2_ExchangeByte(0x02);
  spi2_ExchangeByte(addr);
  spi2_ExchangeByte(payload1);
  spi2_ExchangeByte(payload2);
  delay__ms(2);
  LPC_GPIO0->SET = (1 << 22); // Deselect xCS
}

void mp3SIRead(uint8_t addr) {
  LPC_GPIO0->CLR = (1 << 22); // Select xCS
  delay__ms(2);
  spi2_ExchangeByte(0x03);
  spi2_ExchangeByte(addr);
  printf("%.2x", spi2_ExchangeByte(0xFF));
  printf("%.2x\n", spi2_ExchangeByte(0xFF));
  delay__ms(2);
  LPC_GPIO0->SET = (1 << 22); // Deselect xCS
}

void mp3SoundTest(void) {
  LPC_GPIO0->SET = (1 << 22); // Deselect xCS
  vTaskDelay(5);
  LPC_GPIO0->CLR = (1 << 0); // Select xDCS
  vTaskDelay(5);
  printf("Sound Test: \n");

  printf("%x\n", spi2_ExchangeByte(0x53));
  printf("%x\n", spi2_ExchangeByte(0xEF));
  printf("%x\n", spi2_ExchangeByte(0x6E));
  printf("%x\n", spi2_ExchangeByte(0xAA));
  printf("%x\n", spi2_ExchangeByte(0x00));
  printf("%x\n", spi2_ExchangeByte(0x00));
  printf("%x\n", spi2_ExchangeByte(0x00));
  printf("%x\n", spi2_ExchangeByte(0x00));
  LPC_GPIO0->SET = (1 << 0); // Deselect xDCS
}

void mp3Setup(uint8_t config) {
  // use p0.22 for xCS
  if (config == 1) {
    printf("Configure SCI_Mode Page 38: \n");

    mp3SIWrite(0x00, 0x08, 0x20);

    delay__ms(2);
    LPC_GPIO0->CLR = (1 << 22); // xCS

    mp3SIWrite(0x03, 0x60, 0x00); // use this one
    delay__ms(2);

    mp3SIWrite(0x05, 0x1f, 0x40);
    delay__ms(2);

    mp3SIWrite(0x0b, 0x00, 0x00); // volume
    delay__ms(2);

    LPC_GPIO0->SET = (1 << 22); // xCS

    LPC_GPIO0->CLR = (1 << 22);
    delay__ms(2);
    printf("%x\n", spi2_ExchangeByte(0x03));
    printf("%x\n", spi2_ExchangeByte(0x00));
    printf("%x\n", spi2_ExchangeByte(0xFF));
    printf("%x\n", spi2_ExchangeByte(0xFF));
    delay__ms(2);
    LPC_GPIO0->SET = (1 << 22);
  }

  if (config == 2) {
    // printf("Configure SCI_Mode No TEST Page 38: \n");
    // SCI MODE
    mp3SIWrite(0x00, 0x08, 0x00);
    delay__ms(2);
    // CLK_FREQ
    mp3SIWrite(0x03, 0x60, 0x00); // use this one
    delay__ms(2);
    // WRAM ADDR
    mp3SIWrite(0x07, 0x1e, 0x29);
    delay__ms(2);
    // WRAM
    mp3SIWrite(0x06, 0x00, 0x00);
    delay__ms(2);
    // DECODE TIME
    mp3SIWrite(0x04, 0x00, 0x00);
    delay__ms(2);
    // VOLUME CONTROL
    mp3SIWrite(0x0b, 0x10, 0x10); // volume
    delay__ms(2);
    // AUDATA
    mp3SIWrite(0x05, 0xAC, 0x45);
    delay__ms(2);

    LPC_GPIO0->CLR = (1 << 22);
    delay__ms(2);
    mp3SIRead(0x00);
    delay__ms(2);
    LPC_GPIO0->SET = (1 << 22);
  }
}

void init(void) {
  lpc_peripheral__turn_on_power_to(LPC_PERIPHERAL__SSP1);
  LPC_SSP1->CR0 = 7;
  LPC_SSP1->CR1 |= (1 << 1);
  LPC_SSP1->CPSR = 48;

  gpio__construct_with_function(GPIO__PORT_0, 7, GPIO__FUNCTION_2); // SCK1
  gpio__construct_with_function(GPIO__PORT_0, 9, GPIO__FUNCTION_2); // MOSI1
  gpio__construct_with_function(GPIO__PORT_0, 8, GPIO__FUNCTION_2); // MISO1

  // (xReset) p0.18
  gpio__construct_as_output(GPIO__PORT_0, 18);
  LPC_GPIO0->CLR = (1 << 18);

  // chip select (xCS) p0.22
  gpio__construct_as_output(GPIO__PORT_0, 22);
  LPC_GPIO0->SET = (1 << 22);

  // MP3Request(DREQ) p0.1
  gpio__construct_as_input(GPIO__PORT_0, 1);

  // chip select data (xDCS) p0.0
  gpio__construct_as_output(GPIO__PORT_0, 0);
  LPC_GPIO0->SET = (1 << 0);

  LPC_GPIO0->SET = (1 << 18); // reset

  delay__ms(1000);
  mp3Setup(2);

  // printf(" Initial Register Check: \n");
  // printf(" \n \n Reg 0 (Mode Control 0x800): \n");
  // mp3SIRead(0x00);

  // printf("Reg 1 (Status): \n");
  // mp3SIRead(0x01);

  // printf("Reg 3 (CLOCK Freq 0x6000): \n");
  // mp3SIRead(0x03);

  // printf("Reg 4 (Decode Time): \n");
  // mp3SIRead(0x04);

  // printf("Reg 5 (Aux Data, 0x1f40): \n");
  // mp3SIRead(0x05);

  // LPC_SSP1->CPSR = 4;

  mp3Flush();
}

void sd_startup(void) {
  char place[20] = "1:";
  TCHAR *point1 = place;
  DIR dp;
  FILINFO fno;
  int temp;

  // opening directory
  f_opendir(&dp, point1);

  // reading in SD file names and putting into 2D Char array
  f_readdir(&dp, &fno);
  do {
    if (count == 50) {
      puts("Error. Limit of 50 songs reached. Ending directory read.\n");
      break;
    }
    f_readdir(&dp, &fno);
    // printf("FILE NAME:%s \n", fno.fname);

    for (int i = 0; i < 13; i++) // convert TCHAR TO INT TO CHAR
    {
      temp = fno.fname[i];
      if (fno.fname[i] == 0) {
        while (i < 13) {
          list[count][i] = ' ';
          i++;
        }
        break;
      }
      list[count][i] = temp;
    }
    count++;
  } while (fno.fname[0] != 0);
  f_closedir(&dp); // closing directory
  count--;
}
static bool play_music = false;
static char *input;

void rx_task(void *p) {
  while (1) {
    uart__polled_get(UART__3, &input);
    vTaskDelay(500);
  }
}

void music_control(void *p) {
  while (1) {
    printf("mode: %c\n", input);
    if (input == 'p' && play_music == false) {
      printf("Resuming\n");
      play_music = true;
      vTaskResume(mp3_task);
      vTaskResume(mp3_sdcard);
    } else if (input == 's' && play_music == true) {
      printf("Suspending music\n");
      play_music = false;
      vTaskSuspend(mp3_task);
      vTaskSuspend(mp3_sdcard);
    }
    vTaskDelay(500);
  }
}
void run(void *p) {
  vTaskSuspend(NULL);
  /*while (1) {
    mp3SoundTest();
  }*/
  // mp3Setup(2);
  while (mp3Size < lsize + 32) { /*
     printf(" \n \n Reg 0 (Mode Control 0x800): \n");
     mp3SIRead(0x00);

     printf("Reg 4 (Decode Time): \n");
     mp3SIRead(0x04);

     printf("Reg 5 (Aux Data, 0x1f40): \n");
     mp3SIRead(0x05);*/

    // if (sdready)
    // printf("sdready=true\n");
    // else
    // printf("sdready=false\n");
    while ((songPos < 16) && (sdready == true) && (mp3Sel == 0)) {
      while (!(LPC_GPIO0->PIN & (1 << 1))) {
        // printf("delaying\n");
        vTaskDelay(1);
        ;
      }
      // printf("songPos is %i\n", songPos);
      LPC_GPIO0->CLR = (1 << 0); // Select xDCS
      for (int a = 0; a < 32; a++) {
        spi2_ExchangeByte(songptr[a + (songPos * 32)]);
        // printf("sent %x\n ", songptr[a + (songPos * 32)]);
      }

      // sdready = false;
      songPos++;
      // printf("\n \n %i , %i \n", mp3Size, songPos);
      LPC_GPIO0->SET = (1 << 0); // Deselect xDCS
      if (songPos == 16) {
        mp3Sel = 0;
        sdready = false;
      }
    }
  }
}

void mp3sdcard(void *p) {
  vTaskSuspend(NULL);

  unsigned int bytes_read = 0;

  while (1) {

    // vTaskDelay(300);
    /*char *temphold3[16];
    temphold3[0] = '1';
    temphold3[1] = ':';
    temphold3[15] = '\0';
    for (int i = 0; i < 13; i = i + 1) {
      temphold3[i + 2] = list[current][i];
    }

   printf("mp3SD Card %s\n", temphold3); */

    const char *filename = "Tetris.mp3";

    FIL song;

    // printf("sdcard\n");
    FRESULT check = f_open(&song, filename, (FA_READ | FA_OPEN_ALWAYS));

    // printf("Check: %i\n", check);
    if (FR_OK == check) {
      f_lseek(&song, f_size(&song));
      // printf("f_lseek to end\n");
      lsize = f_tell(&song);
      lsize /= 32;

      f_lseek(&song, 0);
      // printf("f_lseek to beginning\n");
      mp3Size = 0;
      songPos = 0;

      f_read(&song, &songptr, 512, &bytes_read);
      // printf("fread1\n");

      while (mp3Size < lsize + 32) {

        if (songPos == 16 && sdready == false || sdready == false) {
          f_read(&song, &songptr, 512, &bytes_read);
          // printf("fread2\n");
          mp3Size += 16;
          songPos = 0;
          sdready = true;
        }
        vTaskDelay(1);

      }                            // end while
      printf("Song finished! \n"); // keep
      f_close(&song);              // close the file
      songPos = 0;
      mp3Size = 0;
      vTaskDelay(500);
    } // end if(song)
    else {
      printf("Failed to read SD card \n");
    }
    vTaskDelay(1);
  }
}

int main(void) {

  init();
  uint32_t pclk = clock__get_peripheral_clock_hz;
  uart__init(UART__3, pclk, 115200);
  gpio__construct_with_function(GPIO__PORT_4, 29, GPIO__FUNCTION_2); // RX
  // sd_startup();
  xTaskCreate(music_control, "Music_Control", 1024U / sizeof(void *), NULL, PRIORITY_LOW, &mp3_control);
  xTaskCreate(run, "mp3_task", 2048U / sizeof(void *), NULL, PRIORITY_LOW, &mp3_task);
  xTaskCreate(mp3sdcard, "mp3sdcard", 4096U / sizeof(void *), NULL, PRIORITY_LOW, &mp3_sdcard);
  xTaskCreate(rx_task, "rx", 512U / sizeof(void *), NULL, PRIORITY_LOW, NULL);
  // sj2_cli__init();

  puts("\nStarting RTOS\n");
  vTaskStartScheduler();

  return 0;
}
