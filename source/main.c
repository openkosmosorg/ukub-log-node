/*
 * KubOS RT
 * Copyright (C) 2016 Kubos Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

#include "telemetry.h"

#include "kubos-hal/gpio.h"
#include "kubos-hal/uart.h"

#include "kubos-core/modules/fs/fs.h"
#include "kubos-core/modules/fatfs/ff.h"

#include "kubos-core/modules/fatfs/diskio.h"

#include <csp/csp.h>

#define FILE_PATH "data.txt"

/* set at FatFs LFN max length */ 
#define FILE_NAME_BUFFER_SIZE 255
#define DATA_BUFFER_SIZE 128

static inline void blink(int pin) {
    k_gpio_write(pin, 1);
    vTaskDelay(1);
    k_gpio_write(pin, 0);
}

uint16_t open_append(FIL * fp, const char * path)
{
    uint16_t result;
    result = f_open(fp, path, FA_WRITE | FA_OPEN_ALWAYS);
    if (result == FR_OK)
    {
        result = f_lseek(fp, f_size(fp));
        if (result != FR_OK)
        {
            f_close(fp);
        }
    }
    return result;
}

uint16_t write_string(char * str, int len, const char * path)
{
    static FATFS FatFs;
    static FIL Fil;
    uint16_t ret;
    uint16_t bw;
    if ((ret = f_mount(&FatFs, "", 1)) == FR_OK)
    {
        if ((ret = open_append(&Fil, path)) == FR_OK)
        {
            if ((ret = f_write(&Fil, str, len, &bw)) == FR_OK)
            {
                ret = f_close(&Fil);
                blink(K_LED_GREEN);
            }
            f_close(&Fil);
        }
    }
    //f_mount(NULL, "", 0);
    return ret;
}

uint16_t open_file(FATFS * FatFs, FIL * Fil, const char * path)
{
    uint16_t ret;
    if ((ret = f_mount(FatFs, "", 1)) == FR_OK)
    {
        ret = open_append(Fil, path);
    }
    //f_mount(NULL, "", 0);
    return ret;
}


uint16_t just_write(FIL * Fil, char * str, uint16_t len)
{
    uint16_t ret;
    uint16_t bw;
    if ((ret = f_write(Fil, str, len, &bw)) == FR_OK)
    {
        blink(K_LED_GREEN);
    }
    else
    {
        f_close(Fil);
    }
    //f_mount(NULL, "", 0);
    return ret;
}

/**
 * @brief creates a filename that corresponds to the telemetry packet source_id and 
 *        the csp packet address.
 * @param filename_buf_ptr a pointer to the char[] to write to.
 * @param source_id the telemetry packet source_id from packet.source.source_id.
 * @param address the csp packet address from packet->id.src. 
 */
void create_filename(char *filename_buf_ptr, uint8_t source_id, unsigned int address)
{
    int len;

    if (filename_buf_ptr == NULL) {
        return;
    }

    len = snprintf(filename_buf_ptr, FILE_NAME_BUFFER_SIZE, "%hhu%u.csv", source_id, address);

    if(len < 0 || len >= FILE_NAME_BUFFER_SIZE) {
        printf("Filename char limit exceeded. Have %d, need %d + \\0\n", FILE_NAME_BUFFER_SIZE, len);
        len = snprintf(filename_buf_ptr, FILE_NAME_BUFFER_SIZE, "\0");
    }
}


/**
 * @brief creates a formatted log entry from the telemetry packet.
 * @param data_buf_ptr a pointer to the char[] to write to.
 * @param packet a telemetry packet to create a log entry from.
 */
void format_log_entry(char *data_buf_ptr, telemetry_packet packet) {
    
    int len;

    if (data_buf_ptr == NULL) {
        return;
    }

    if(packet.source.data_type == TELEMETRY_TYPE_INT) {
        len = snprintf(data_buf_ptr, DATA_BUFFER_SIZE, "%hu,%d\r\n", packet.timestamp, packet.data.i);
        if(len < 0 || len >= DATA_BUFFER_SIZE) {
            printf("Data char limit exceeded for int packet. Have %d, need %d + \\0\n", DATA_BUFFER_SIZE, len);
            len = snprintf(data_buf_ptr, FILE_NAME_BUFFER_SIZE, "\0");
        }
    }

    if(packet.source.data_type == TELEMETRY_TYPE_FLOAT) {
        len = snprintf(data_buf_ptr, DATA_BUFFER_SIZE, "%hu,%f\r\n", packet.timestamp, packet.data.f);
        if(len < 0 || len >= DATA_BUFFER_SIZE) {
            printf("Data char limit exceeded for float packet. Have %d, need %d + \\0\n", DATA_BUFFER_SIZE, len);
            len = snprintf(data_buf_ptr, FILE_NAME_BUFFER_SIZE, "\0");
        }
    }
}

void task_logging(void *p)
{
    static FATFS FatFs;
    static FIL Fil;
    char buffer[128];
    static char filename_buffer[FILE_NAME_BUFFER_SIZE];
    static char *buf_ptr;
    static char data_buffer[DATA_BUFFER_SIZE];
    static char *data_buf_ptr;
    uint16_t num = 0;
    uint16_t sd_stat = FR_OK;
    uint16_t sync_count = 0;
    
    buf_ptr = filename_buffer;
    data_buf_ptr = data_buffer;

    sd_stat = open_file(&FatFs, &Fil, FILE_PATH);
    while (1)
    {
        while ((num = k_uart_read(K_UART_CONSOLE, buffer, 128)) > 0)
        {
            if (sd_stat != FR_OK)
            {
                blink(K_LED_RED);
                f_close(&Fil);
                vTaskDelay(50);
                f_mount(NULL, "", 0);
                vTaskDelay(50);
                sd_stat = open_file(&FatFs, &Fil, FILE_PATH);
                vTaskDelay(50);
            }
            else
            {
                blink(K_LED_BLUE);
                sd_stat = just_write(&Fil, buffer, num);
                // Sync every 20 writes
                if (sd_stat == FR_OK)
                {
                    sync_count++;
                    if ((sync_count % 20) == 0)
                    {
                        sync_count = 0;
                        f_sync(&Fil);
                    }
                }
            }
        }
    }
}

int main(void)
{
    k_uart_console_init();

    k_gpio_init(K_LED_GREEN, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_ORANGE, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_RED, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_BLUE, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_BUTTON_0, K_GPIO_INPUT, K_GPIO_PULL_NONE);

    xTaskCreate(task_logging, "logging", configMINIMAL_STACK_SIZE * 5, NULL, 2, NULL);

    vTaskStartScheduler();

    return 0;
}
