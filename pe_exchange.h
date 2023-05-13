#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"
#include <sys/epoll.h>
#include <sys/signalfd.h>

#define LOG_PREFIX "[PEX]"

#define TRADERS_START 2
#define BUF_SIZE 256 // temporary storage for message strings
#define CMD_LEN 7 // longest possible command type a trader can send
#define OID_MIN 0
#define OID_MAX 999999
#define ORDER_MIN 1
#define ORDER_MAX 999999

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
    int trader_id; // trader that made the order
    char *product;
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
    int max_buy_order_id; // ensures BUY OIDs are consecutive
    int max_sell_order_id; // ensures SELL OIDs are consecutive
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
 * Desc: Initializes the matches matrix and sets all entries to default values.
 * Params: The matches matrix, the number of traders and the number of products.
 */
void init_matches(int ****matches, int num_traders, int prods_size);

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
 * Return: 0 on successful read, 1 otherwise
 */
int read_and_format_message(trader *curr_trader, char *message_in);

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
int execute_command(trader *curr_trader, char *message_in, int cmd_type, products *prods, int *product_index, order ***buys, order ***sells, trader *head);

/*
 * Desc: Finds matching orders for product at product_index, prints the 
         corresponding match message to stdout and notifies involved traders of
         the trader.
 * Params: Pointers to the match, buy, sell and trader lists the index of the product
           to find matches for.
 */
void find_matches(int ****matches, order ***buys, order ***sells, trader *head, double* total_trading_fees, int product_index);

/*
 * Desc: Prints the orderbook to stdout.
 * Params: Pointers to the products list, buy and sell orders.
 */
void display_orderbook(products *prods, order **buys, order **sells);

/*
 * Desc: Counts the number of orders for a specific product.
 * Params: A pointer to the list to count orders from, the index of the product
           to count orders for.
 * Return: The number of orders in list, -1 otherwise.
 */
int count_order_levels(order **list, int product_index);

/*
 * Desc: Prints all unique orders for a specific product at product_index to
         stdout.
 * Params: A pointer to the order list, the product_index of the product to count
           and a flag indicating that we are printing buy or sell orders
 */
void display_orders(order **list, int product_index, int order_type);

/*
 * Desc: Prints the positions of all traders to stdout.
 * Params: A pointer to the head of the traders list, the matches matrix and 
           a pointer to the products struct.
 */
void display_positions(trader *head, int ***matches, products *prods);

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
void free_structs(products *prods, trader *head, order **buys, order **sells);

/*
 * Desc: Frees the memory used by the products struct.
 * Param: a pointer to the products struct.
 */
void free_products_list(products *prods);

/*
 * Desc: Frees memory used by the trader list, and frees memory used by each
         trader structure's dynamic fields.
 * Params: A pointer to the head of the trader linked list.
 */
void free_trader_list(trader *head);

/*
 * Desc: Frees memory used by the order list (buy / sell orders).
 * Params: A pointer to the order list head and a pointer to the products struct.
 */
void free_order_list(order **order_list, products *prods);

/*
 * Desc: Frees all allocated dimensions used by the matches matrix.
 * Param: The matches matrix, the number of traders and the number of products.
 */
void free_matches(int ***matches, int num_traders, int prods_size);

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
