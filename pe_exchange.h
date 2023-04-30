#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"

#define LOG_PREFIX "[PEX]"

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

#endif
