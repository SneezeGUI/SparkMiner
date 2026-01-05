#ifndef __DRIVER_SDMMC_H
#define __DRIVER_SDMMC_H

#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
#include <list>  // Include list header file

// Define a structure to store file and directory information
struct File_Entry {
    String name;
    bool isDirectory;
    size_t size;  // Valid only for files
};

void sdmmc_init(uint8_t clkPin, uint8_t cmdPin, uint8_t d0Pin, uint8_t d1Pin, uint8_t d2Pin, uint8_t d3Pin);
std::list<File_Entry> list_dir(const char * dirname, uint8_t levels);
void create_dir(const char * path);
void remove_dir(const char * path);
size_t read_file_size(const char *path);
void read_file(const char * path, uint8_t *buffer, size_t length);
void write_file(const char * path, const uint8_t * buffer, size_t size);
void append_file(const char * path, const uint8_t * buffer, size_t size);
void rename_file(const char * path1, const char * path2);
void delete_file(const char * path);
void test_file_io(const char * path);

int read_file_num(const char * dirname);
void print_file_list(const std::list<File_Entry>& fileList);
String get_file_name_by_index(const char * dirname, int index);
void write_jpg(const char * path, const uint8_t *buf, size_t size);
void write_bmp(char *path, uint8_t *buf, long size, long height, long width);

bool write_wav_header(const char * path, uint32_t data_size);

#endif