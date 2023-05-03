#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"
#include <sys/epoll.h>

#define LOG_PREFIX "[PEX]"

#define TRADERS_START 2

/*
 * Desc: Generic order struct.
 * Fields: string representing the product type of the order, and ints for the
           quantity of the product and the price per unit. Also has a pointer
           to the next order in the list.
 */
typedef struct order order;
struct order {
    char product[PRODUCT_STR_LEN];
    int quantity;
    int price;
    order *next;
};

/*
 * Desc: All-encompassing trader struct.
 * Fields: Tracks the trader ID, process ID of the
         trader binary, sell and buy orders, the minimum sell and maximum buy
         orders, the array of file descriptors and a pointer to the next trader
         in the list.
 */
typedef struct trader trader;
struct trader {
    int trader_id; // main identifier
    pid_t process_id; // get this from the fork() call
    order *buy_orders;
    order *sell_orders;
    order *min_sell;
    order *max_buy;
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
 * Desc: Reads the provided product file and initializes a products struct
         with the information in that file. This functions gets the number of
         products our exchange will trade as well as the names of each product.
 * Params: The path to the product file.
 * Return: A pointer to the products struct.
 */
int initialize_product_list(char product_file[], products *prods);

/*
 * Desc: Creates named pipes, launches trader process and connects to the
         corresponding named pipes, based on trader ID. Creates and initializes
         a new trader struct and adds it to the list. Also prints the 
         necessary messages to stdout.
 * Params: The number of traders to spawn, the list of command line args given
           to pe_exchange and a pointer to a pointer to the head of the 
           trader list.
 * Return: 
 */
int spawn_and_communicate(int num_of_traders, char **argv, trader **head);

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
 * Desc: Closes, flushes and deletes all fifos created. Used during shutdown
 */
void cleanup_fifos(int number_of_traders);

#endif
