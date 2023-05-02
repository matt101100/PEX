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
 * Desc: Order book struct.
 * Fields: Holds pointers to the sell_orders struct, which contains all current
           buy orders and similarly hold a pointer to the sell_orders struct.
 */
typedef struct order_book order_book;
struct order_book {
    order *buy_orders;
    order *sell_orders;
};

/*
 * Desc: 
 * Fields: 
 */
typedef struct trader trader;
struct trader {
    int trader_id;
    order_book *ob;
    order *min_sell;
    order *max_buy;
    trader *next;
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
 * Desc: Node struct which holds information regarding each traders set of fds
 * Fields: the trader ID, the set of fds and a pointer to the next node in list
 */
typedef struct pipe_node pipe_node;
struct pipe_node {
    int trader_id;
    int fd[2];
    pipe_node *next;
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
         corresponding named pipes, based on trader ID. Also prints the 
         necessary messages to stdout.
 * Params: The number of traders to spawn, a path to the exchange and trader fifos
 * Return: 
 */
int spawn_and_communicate(int num_of_traders);

/*
 * Desc: calls all free functions to free allocated memory used for the 
         corresponding structs.
 * Params: pointers to all structs
 */
void free_structs(products *prods);

/*
 * Desc: Frees the memory used by the products struct.
 * Param: a pointer to the products struct.
 */
void free_products_list(products *prods);

/*
 * Desc: Frees the memory allocated for strings, such as the filepaths
 * Param: Pointers that were dynamically allocated to hold strings
 */
void free_strings(char *exchange_fifo_path, char *trader_fifo_path);

/*
 * Desc: Closes, flushes and deletes all fifos created. Used during shutdown
 */
void cleanup_fifos(int number_of_traders);

#endif
