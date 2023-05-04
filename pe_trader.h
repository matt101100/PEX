#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"

#define BUY "BUY %d %s %d %d;"
#define SELL "SELL %d %s %d %d;"

#define FILEPATH_LEN 100 // enough space to hold any filepath string
#define MESSAGE_LEN 100

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
 * Return: The initialized sigaction struct
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

/*
 * Desc: Reads from the exchange named pipe and formats the message as a null-
         terminated string
 * Params: The fd to read from
 * Return: The number of bytes read
 */
int read_exchange_msg(int read_fd);

/*
 * Desc: Produces an order string with correct formatting to send to exchange
 * Params: The fd to write to, string representing the sell_order we must buy
 * Return: 1 on successful creation and writing of buy order, 0 otherwise
 */
int format_order(int write_fd, char sell_order[]);

/*
 * Desc: Writes to the trader named pipe
 * Params: The fd to write to, the string to write
 * Return: The number of bytes written
 */
int write_to_exchange(int write_fd, char to_write[]);

#endif
