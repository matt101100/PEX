#ifndef PE_COMMON_H
#define PE_COMMON_H

#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FIFO_EXCHANGE "/tmp/pe_exchange_%d"
#define FIFO_TRADER "/tmp/pe_trader_%d"
#define FEE_PERCENTAGE 1

#define FILEPATH_LEN 100 // enough space to hold any filepath string
#define MESSAGE_LEN 100
#define PRODUCT_STR_LEN 17 // + 1 for null terminator

#endif
