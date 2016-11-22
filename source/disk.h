#ifndef DISK_H
#define DISK_H

#include <kubos-core/modules/fs/fs.h>
#include <kubos-core/modules/fatfs/ff.h>
#include <kubos-core/modules/fatfs/diskio.h>

uint16_t open_append(FIL * fp, const char * path);
uint16_t write_string(char * str, int len, const char * path);
uint16_t open_file(FATFS * FatFs, FIL * Fil, const char * path);
uint16_t just_write(FIL * Fil, char * str, uint16_t len);

#endif
