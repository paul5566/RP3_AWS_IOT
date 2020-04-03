#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board_io.h"
#include "common_macros.h"
#include "gpio.h"
#include "sj2_cli.h"

#include "cli_handlers.h"
#include "ff.h"

#include "ssp0_lab.h"

/*****register command*****/
uint16_t MODE = 0x4810;         // 4800
uint16_t CLOCKF = 0xE000;       // clock frequency was 9800, EBE8, B3E8, BBE8
volatile uint16_t VOL = 0x2222; // full vol
uint16_t BASS = 0x0076;         // was 00F6
uint16_t AUDATA = 0xAC80;       // for stereo decoding, AC45,AC80, BB80-check
uint16_t STATUS;
// VS10xx SCI Registers
#define SCI_MODE 0x00
#define SCI_STATUS 0x01
#define SCI_BASS 0x02
#define SCI_CLOCKF 0x03
#define SCI_DECODE_TIME 0x04
#define SCI_AUDATA 0x05
#define SCI_WRAM 0x06
#define SCI_WRAMADDR 0x07
#define SCI_HDAT0 0x08
#define SCI_HDAT1 0x09
#define SCI_AIADDR 0x0A
#define SCI_VOL 0x0B
#define SCI_AICTRL0 0x0C
#define SCI_AICTRL1 0x0D
#define SCI_AICTRL2 0x0E
#define SCI_AICTRL3 0x0F
#define playSpeed 0x1e04

// READ WRITE register commands
#define READ 0x03
#define WRITE 0x02

// static uint8_t SEND_NUM_BYTES = 32;
static uint16_t READ_BYTES_FROM_FILE = 4096;
static uint16_t TRANSMIT_BYTES_TO_DECODER = 32;

/*GLOBAL variable*/
typedef char songname[32];
const int length_512 = 512;

/*QueueHandle for MP3*/
QueueHandle_t Q_songname;
QueueHandle_t Q_songdata;
QueueHandle_t Q_filename;

static void dec_cs(void) {
  LPC_IOCON->P0_6 &= ~(7 << 0);
  LPC_GPIO0->DIR |= (1 << 6);
  LPC_GPIO0->CLR |= (1 << 6);
}
static void dec_ds(void) {
  LPC_IOCON->P0_6 &= ~(7 << 0);
  LPC_GPIO0->DIR |= (1 << 6);
  LPC_GPIO0->SET |= (1 << 6);
}
static void dec_dcs(void) {
  LPC_IOCON->P0_7 &= ~(7 << 0);
  LPC_GPIO0->DIR |= (1 << 7);
  LPC_GPIO0->CLR |= (1 << 7);
}
static void dec_dds(void) {
  LPC_IOCON->P0_7 &= ~(7 << 0);
  LPC_GPIO0->DIR |= (1 << 7);
  LPC_GPIO0->SET |= (1 << 7);
}

static void dec_rst(void) {
  LPC_IOCON->P0_9 &= ~(7 << 0);
  LPC_GPIO0->DIR |= (1 << 9);
  LPC_GPIO0->CLR |= (1 << 9);
  vTaskDelay(100);
  LPC_GPIO0->SET |= (1 << 9);
  vTaskDelay(100);
}

/*0_9 -> 4_28*/
/*
void dec_rst(void) {
  LPC_IOCON->P4_28 &= ~(7 << 0);
  LPC_GPIO4->DIR |= (1 << 28);
  LPC_GPIO4->CLR |= (1 << 28);
  vTaskDelay(100);
  LPC_GPIO4->SET |= (1 << 28);
  vTaskDelay(100);
}
*/

static void dec_dreq_set_as_input(void) {
  LPC_IOCON->P0_8 &= ~(0x7 << 0);
  LPC_GPIO0->DIR &= ~(1 << 8);
}

/*0_8 ->0_10*/
/*
void dec_dreq_set_as_input(void) {
  LPC_IOCON->P0_10 &= ~(0x7 << 0);
  LPC_GPIO0->DIR &= ~(1 << 10);
}
*/
/*
void spi_ds_1_10(void) {
  LPC_IOCON->P1_10 &= ~(7 << 0);
  LPC_GPIO1->DIR |= (1 << 10);
  LPC_GPIO1->SET |= (1 << 10);
}
*/

typedef struct {
  uint8_t first_byte;
  uint8_t second_byte;
  uint8_t third_byte;
  uint8_t forth_byte;
} mp3_dec;

mp3_dec mp3_dec_read(uint8_t first, uint8_t second, uint8_t third, uint8_t forth) {
  mp3_dec data = {1, 2, 3, 4};
  dec_cs();
  {
    // Send opcode and read bytes
    // TODO: Populate members of the 'adesto_flash_id_s' struct
    // ssp0__exchange_byte_lab(0x03);
    data.first_byte = ssp0__exchange_byte_lab(first);
    data.second_byte = ssp0__exchange_byte_lab(second);
    data.third_byte = ssp0__exchange_byte_lab(third);
    data.forth_byte = ssp0__exchange_byte_lab(forth);
  }
  dec_ds();
  return data;
}

mp3_dec mp3_dec_d_write(uint8_t first, uint8_t second, uint8_t third, uint8_t forth) {
  /*enable the data chip select*/
  dec_dcs();
  {
    ssp0__exchange_byte_lab(first);
    ssp0__exchange_byte_lab(second);
    ssp0__exchange_byte_lab(third);
    ssp0__exchange_byte_lab(forth);
    ssp0__exchange_byte_lab(0);
    ssp0__exchange_byte_lab(0);
    ssp0__exchange_byte_lab(0);
    ssp0__exchange_byte_lab(0);
  }
  /*diaable the data chip select*/
  dec_dds();
}

// mp3 play application with coommand
app_cli_status_e cli__mp3_play(app_cli__argument_t argument, sl_string_t user_input_minus_command_name,
                               app_cli__print_string_function cli_output) {
  // sl_string is a powerful string library, and you can utilize the sl_string.h API to parse parameters of a command

  sl_string_t s = user_input_minus_command_name;

  songname name;
  // for(int i=0; i<32;i++){
  //   if(s[i] == '\0')
  //     break;
  //   *name = sl_string__c_str(&s);
  // }

  // sl_string__printf(s, "\n");
  int c = 0;
  while (c < 32 - 5) {
    if (s[c] == '.') {
      break;
    }
    // sl_string__append_char(s, s[c]);
    name[c] = sl_string__c_str(s[c]);
    c++;
  }
  for (int i = 0; i < 4; i++) {
    name[c] = sl_string__c_str(s[c]);
    c++;
  }
  name[c] = '\0';
  // cli_output(NULL, s);

  // printf("\nsize: %d\nname: %s\ns: %s\nname 1 : %c\nname 2 : %c\n", sl_string__get_length(s), name, s, name[0],
  // name[1]);
  // xQueueSend(Q_songname, &name, portMAX_DELAY);

  // printf("Sent %s over to the Q_songname\n", user_input_minus_command_name);
  return APP_CLI_STATUS__SUCCESS;
}

// Reader tasks receives song-name over Q_songname to start reading it
void mp3_reader_task(void *p) {
  songname name;
  // xQueueSend(Q_songname, &name, portMAX_DELAY);
  char bytes_512[length_512];
  FIL file; // File handle
  UINT byte_read = 0;
  int file_size;
  int byte_been_read;
  while (1) {
    xQueueSend(Q_songname, &name, portMAX_DELAY);
    xQueueReceive(Q_songname, &name, portMAX_DELAY);
    printf("Received song to play: %s\n", name);

    const char *filename = "1.mp3";
    FRESULT result = f_open(&file, filename, FA_READ); // open_file();

    if (FR_OK == result) {
      file_size = f_size(&file);
      printf("file_size: %u\n", file_size);
      byte_been_read = 0;
      while (byte_been_read < file_size) {
        f_read(&file, &bytes_512, length_512, &byte_read);
        xQueueSend(Q_songdata, &bytes_512, portMAX_DELAY);
        byte_been_read += byte_read;
      }
      f_close(&file);
      printf("\nend of file. \n");
      printf("from reader: size %d real_size %d\n", file_size, byte_been_read);
    } else {
      printf("ERROR: Failed to open: %s\n", filename);
    }
  }
}
bool mp3_dreq_getLevel(void) { return ((LPC_GPIO0->PIN >> 8) & 1); }
bool mp3_decoder_needs_data(void) { return mp3_dreq_getLevel(); }
// Player task receives song data over Q_songdata to send it to the MP3 decoder
void mp3_player_task(void *p) {
  char bytes_512[length_512];
  // int size;
  while (1) {
    // vTaskDelay(1000);
    // printf("\n");
    xQueueReceive(Q_songdata, &bytes_512, portMAX_DELAY);
    // size=0;
    dec_dcs();
    vTaskDelay(6);
    for (int i = 0; i < sizeof(bytes_512); i += 32) {
      while (!mp3_decoder_needs_data()) {
        vTaskDelay(5);
      }
      for (int j = 0; j < 32; j++) {
        ssp0__exchange_byte_lab(bytes_512[i + j]);
        // printf("0x%x, ", bytes_512[i+j]);
      }
      while (!mp3_decoder_needs_data()) {
        vTaskDelay(5);
      }
    }
    // size+=512;
    dec_dds();
    // printf("from sender: %d\n", size);
    // vTaskDelay(10);
    // printf("\n");
  }
}

FRESULT scan_files(char *path /* Start node to be scanned (***also used as work area***) */
) {
  FRESULT res;
  DIR dir;
  UINT i;
  static FILINFO fno;

  res = f_opendir(&dir, path); /* Open the directory */
  if (res == FR_OK) {
    for (;;) {
      res = f_readdir(&dir, &fno); /* Read a directory item */
      if (res != FR_OK || fno.fname[0] == 0)
        break;                    /* Break on error or end of dir */
      if (fno.fattrib & AM_DIR) { /* It is a directory */
        i = strlen(path);
        sprintf(&path[i], "/%s", fno.fname);
        res = scan_files(path); /* Enter the directory */
        if (res != FR_OK)
          break;
        path[i] = 0;
      } else { /* It is a file. */
        printf("%s/%s\n", path, fno.fname);

        // find mp3
        if (fno.fname[strlen(fno.fname) - 1] == '3') {
          int i = 0;
          while (fno.fname[i] != '\0') {
            printf("%c", fno.fname[i]);
            i++;
          }
          printf("\n");
          // send to task(fno.fname)
          xQueueSend(Q_filename, &fno.fname, portMAX_DELAY);
        }
      }
    }
    f_closedir(&dir);
  }
  return res;
}
void mp3_writeRequest(uint8_t address, uint16_t data) {
  while (!mp3_dreq_getLevel())
    ;
  dec_cs(); // do a chip select
  ssp0__exchange_byte_lab(WRITE);
  ssp0__exchange_byte_lab(address);
  uint8_t dataUpper = (data >> 8 & 0xFF);
  uint8_t dataLower = (data & 0xFF);
  ssp0__exchange_byte_lab(dataUpper);
  ssp0__exchange_byte_lab(dataLower);
  while (!mp3_dreq_getLevel())
    ;       // dreq should go high indicating transfer complete
  dec_ds(); // Assert a HIGH signal to de-assert the CS
}
bool mp3_initDecoder() {
  mp3_writeRequest(SCI_MODE, MODE);
  mp3_writeRequest(SCI_CLOCKF, CLOCKF);
  // mp3_writeRequest(SCI_VOL, VOL);
  // mp3_writeRequest(SCI_BASS, BASS);
  // mp3_writeRequest(SCI_AUDATA, AUDATA);

  // mp3_writeRequest(SCI_AICTRL0, 0x7000);
  // mp3_writeRequest(SCI_AICTRL1, 0x7000);
  // mp3_writeRequest(SCI_AIADDR, 0x4020);
  return true;
}
void dec_init() {
  // spi_ds_1_10();

  dec_dreq_set_as_input();
  dec_ds();
  dec_dds();

  vTaskDelay(100);
  dec_rst();

  vTaskDelay(100);
  mp3_initDecoder();
  vTaskDelay(100);
}

void task_spi(void *pvParameters) {
  //
  mp3_dec id = {0x01, 0x02, 0x03, 0x04};
  vTaskDelay(1000);
  dec_init();
  vTaskDelay(100);
  // mp3_dec_d_write(0x53, 0xEF, 0x6E, 0x7E);
  // mp3_dec_d_write(0, 0, 0, 0);
  for (uint8_t i = 0; i < 16; i++) {
    id = mp3_dec_read(0x03, i, 0x12, 0x34);
    printf("dec_write: %x, %x, %x, %x\n", id.first_byte, id.second_byte, id.third_byte, id.forth_byte);
    vTaskDelay(100);
  }
  while (1) {
    vTaskDelay(10);
  }
}

int main(void) {
  sj2_cli__init();

  Q_songname = xQueueCreate(1, 32); //(sizeof(songname), 1)
  Q_songdata = xQueueCreate(1, length_512);
  Q_filename = xQueueCreate(1, 32);

  ssp0__init_lab(1);
  xTaskCreate(task_spi, "dec_spi", (512U * 8 / sizeof(void *)), NULL, PRIORITY_HIGH, NULL);
  // mp3
  xTaskCreate(mp3_reader_task, "reader", (512U * 8 / sizeof(void *)), NULL, PRIORITY_MEDIUM, NULL);
  xTaskCreate(mp3_player_task, "player", (512U * 8 / sizeof(void *)), NULL, PRIORITY_MEDIUM, NULL);
  puts("Starting RTOS");
  vTaskStartScheduler(); // This function never returns unless RTOS scheduler runs out of memory and fails

  return 0;
}
