#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"

#define BUY "BUY %d %s %d %d;"
#define SELL "SELL %d %s %d %d;"

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
