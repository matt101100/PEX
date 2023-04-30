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
#include <errno.h>

#define FIFO_EXCHANGE "/tmp/pe_exchange_%d"
#define FIFO_TRADER "/tmp/pe_trader_%d"
#define FEE_PERCENTAGE 1

#define FILEPATH_LEN 100 // enough space to hold any filepath string
#define MESSAGE_LEN 100
#define PRODUCT_STR_LEN 17 // + 1 for null terminator

// flags representing connection types to named pipes
enum connection_type {
    READ = 0,
    WRITE
};

/*
 * Desc: SIGUSR1 handler -- trader reads from the pe_exchange_*
         FIFO in order to obtain the message sent by pe_exchange
 * Params: The int representing SIGUSR1, which we handle
 */
void sigusr1_handle(int signum);

/*
 * Desc: Creates and initializes the sigaction struct, setting the handler to
         my custom sigusr1 handler
 * Return: The sigaction struct
 */
struct sigaction initialize_signal_action(void);

/*
 * Desc: Formats the file path with the trader's corresponding trader ID and 
         opens the named pipe at that location.
 * Params: Flag representing the type of connection the pipe will be opened for
           and the trader ID
 * Return: The file descriptor associated with the named pipe
 */
int connect_to_named_pipe(int connection_type, int trader_id);

#endif
