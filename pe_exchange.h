#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"
#include <sys/epoll.h>
#include <sys/signalfd.h>

#define LOG_PREFIX "[PEX]"

#define TRADERS_START 2
#define BUF_SIZE 256 // temporary storage for message strings
#define CMD_LEN 7 // longest possible command type a trader can send

enum cmd_type {
    BUY = 0,
    SELL,
    AMMEND,
    CANCEL
};

/*
 * Desc: Generic order struct.
 * Fields: string representing the product type of the order, and ints for the
           quantity of the product and the price per unit. Also has a pointer
           to the next order in the list.
 */
typedef struct order order;
struct order {
    int order_id;
    char product[PRODUCT_STR_LEN];
    int product_index; // index of the product string in the string array
    int quantity;
    int price;
    order *next;
};

/*
 * Desc: All-encompassing trader struct.
 * Fields: Tracks the trader ID, process ID of the trader binary,
           the array of file descriptors and a pointer to the next trader.
 */
typedef struct trader trader;
struct trader {
    int trader_id; // main identifier
    pid_t process_id; // get this from the fork() call
    int fd[2]; // fd[0] = trader fifo, fd[1] = exchange fifo
    trader *next; // has a linked-list structure
};

/*
 * Desc: Holds the list of products that the exchange will trade.
 * Fields: An int representing the number of products that will be traded and
           a pointer to a list of string pointers, where the strings will be the
           product names.
 */
typedef struct products products;
struct products {
    int size;
    char **product_strings; // pointer to a list of string pointers
};

/*
 * Desc: SIGUSR1 and SIGCHLD handler, used by pe_exchange
 * Params: The signal, a pointer to the siginfo_t struct and a context pointer
 */
void signal_handle(int signum, siginfo_t *info, void *context);

/*
 * Desc: Initializes the sigaction struct
 * Params: a pointer to a sigaction struct
 */
void init_sigaction(struct sigaction *sa);

/*
 * Desc: Reads the provided product file and initializes a products struct
         with the information in that file. This functions gets the number of
         products our exchange will trade as well as the names of each product.
 * Params: The path to the product file.
 * Return: A pointer to the products struct.
 */
int init_product_list(char product_file[], products *prods);

/*
 * Desc: Creates named pipes, launches trader process and connects to the
         corresponding named pipes, based on trader ID. Creates and initializes
         a new trader struct and adds it to the list. Also prints the 
         necessary messages to stdout.
 * Params: The number of traders to spawn, the list of command line args given
           to pe_exchange and a pointer to a pointer to the head of the 
           trader list.
 * Return: 0 if all traders where successfully set up and exec'd, 1 otherwise
 */
int spawn_and_communicate(int num_traders, char **argv, trader **head);

/*
 * Desc: Reads and formats message from trader_fifo of trader with matching PID.
 * Params: the pid to match and a pointer to the head of the trader list
 */
char *read_and_format_message(trader *curr_trader);

/*
 * Desc: Determines the type of command to execute specified by the message.
 * Params: The message to parse.
 * Returns: A flag representing the command type
*/
int determine_cmd_type(char *message_in);

/*
 * Desc: Acts on the command sent via a fifo and responds accordingly to the
         corresponding trader.
 * Params: The PID to match, a pointer to the head of trader list,
           the command to parse and execute.
 * Return: 0 on successful parsing and execution of the command, 1 otherwise
 */
int execute_command(trader *curr_trader, char *message_in, int cmd_type, order ***buys, order ***sells);

/*
 * Desc: Gets the trader with matching PID or trader ID.
 * Params: the PID / TID to match, a pointer the head of the trader list
 */
trader *get_trader(pid_t pid, int trader_id, trader *head);

/*
 * Desc: Gets the index of product in the products string array.
 * Params: A pointer to the products struct containing the product array, the
           product string to find.
 * Return: The index of the product string in the string array, -1 if invalid.
 */
int get_product_index(products *prods, char *product);

/*
 * Desc: calls all free functions to free allocated memory used for the 
         corresponding structs.
 * Params: pointers to structs
 */
void free_structs(products *prods, trader *head);

/*
 * Desc: Frees the memory used by the products struct.
 * Param: a pointer to the products struct.
 */
void free_products_list(products *prods);

/*
 * Desc: Frees memory used by the trader list, and frees memory used by each
         trader structure's dynamic fields.
 * Param: A pointer to the head of the trader linked list.
 */
void free_trader_list(trader *head);

/*
 * Desc: Closes and deletes FIFOs, frees memory used by the trader with matching
         PID.
 * Params: The PID of the trader to be terminated, pointer to the head of trader
           list.
 */
void cleanup_trader(pid_t pid, trader **head);

/*
 * Desc: Closes, flushes and deletes all fifos created. Used during shutdown
 */
void cleanup_fifos(int number_of_traders);

#endif
