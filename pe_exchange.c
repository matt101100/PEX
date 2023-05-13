/**
 * comp2017 - assignment 3
 * Matthew Lazar
 * mlaz7837
 * 490431454
 */

#include "pe_exchange.h"

volatile sig_atomic_t sigusr1 = 0;
volatile sig_atomic_t sigchld = 0;
volatile sig_atomic_t pid = -1; // stores the ID of the trader that last signaled

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Invalid number of arguments provided.\n");
		return 1;
	}

	// setup the sigaction struct
	struct sigaction sa;
	init_sigaction(&sa);
	if (sigaction(SIGUSR1, &sa, NULL) == -1 
		|| sigaction(SIGCHLD, &sa, NULL) == -1) {
        printf("Error initializing sigaction.\n");
        return 1;
    }

	int res = 0; // stores result of init functions for error checking
	// int bytes_read = -1;
	int bytes_written = -1;
	int num_traders = argc - TRADERS_START;

	printf("%s Starting\n", LOG_PREFIX);

	// initialize structs and prepare for exchange launch
	products prods;
	res = init_product_list(argv[1], &prods);
	if (res) {
		printf("Error initializing products list using file %s.\n", argv[1]);
		goto cleanup;
	}

	trader *head = NULL;
	res = spawn_and_communicate(num_traders, argv, &head);
	if (res) {
		printf("Error: %s\n", strerror(errno));
		goto cleanup;
	} else if (head == NULL) {
		printf("Error connecting to traders.\n");
		goto cleanup;
	}

	/*
	 * Order lists -- each index i stores the head of a linked list for the
	   product associated with index i.
	 * Buys are sorted in descending order ells are sorted in asscending order.
	 * Indices are mapped from the products.txt input. Ex: if products
	   are [GPU, CPU] then GPU --> 0, CPU --> 1, so buys[0] is the head of
	   the buy GPU buy orders.
	 */
	order **buys = (order**)calloc(prods.size, sizeof(order*));
	order **sells = (order**)calloc(prods.size, sizeof(order*));

	// initialize the match cache
	int ***matches = NULL;
	init_matches(&matches, num_traders, prods.size);

	/*
	 * Explanation of how the 'match cache' works:
	 		Each trader, i, has a num_products x 2 matrix associated to it.
			Each index j represents an array of size 2 that stores the amount of
			product j owned / owed by trader i and the amount of money owned / 
			owed by trader i for product j. The quantity of product j is stored 
			at index 0, and the value is stored at index 1.
			For example, we have the product list [APPLES, STRAWBERRY] and 
			two traders T0, T1. If you wanted to check how many APPLES T1 owns
			you would access the match cache as follows: matches[1][0][0].
			Breakdown: matches[1] --> access T1's matrix, matches[1][0] --> access
			the product 0 (APPLES) position array, matches[1][0][0] --> access
			the amount of APPLES owned by T1.
	 */

	// send MARKET OPEN; to all traders and signal SIGUSR1
	trader *current = head;
	while (current != NULL) {
		bytes_written = write(current->fd[1], "MARKET OPEN;", strlen("MARKET OPEN;"));
		if (bytes_written < 0) {
			printf("Error: %s\n", strerror(errno));
		}
		kill(current->process_id, SIGUSR1);
		current = current->next;
	}

	// event loop
	double total_fees = 0;
	int trader_disconnect = 0; // counts number of traders disconnected
	int cmd_type = -1;
	// position of the most recently added product in the product strings array
	int product_index = -1;
	char message_in[BUF_SIZE];
	trader *curr_trader = NULL; // tracks the last trader that signalled
	while (trader_disconnect < num_traders) {
		if (!sigchld && !sigusr1) {
			// wait for either signal
			pause();
		}
		if (sigusr1) {
			sigusr1 = 0; // reset flag

			// parse input of trader that sent sigusr1 and return corresponding output
			curr_trader = get_trader(pid, -1, head);
			read_and_format_message(curr_trader, message_in);
			printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, curr_trader->trader_id, message_in);
			cmd_type = determine_cmd_type(message_in);
			res = execute_command(curr_trader, message_in, cmd_type, &prods, &product_index, &buys, &sells, head);
			if (res) {
				// notify trader of invalid message
				write(curr_trader->fd[1], "INVALID;", strlen("INVALID;"));
				kill(curr_trader->process_id, SIGUSR1);
				continue;
			}
			find_matches(&matches, &buys, &sells, head, &total_fees, product_index);
			display_orderbook(&prods, buys, sells);
			display_positions(head, matches, &prods);

		} else if (sigchld) {
			sigchld = 0; // reset flag

			// perform disconnection and cleanup of terminated trader
			curr_trader = get_trader(pid, -1, head);
			curr_trader->disconnected = 1; // disconnect trader
			printf("%s Trader %d disconnected\n", LOG_PREFIX, curr_trader->trader_id);
			trader_disconnect++;
		}
	}

	printf("%s Trading completed\n", LOG_PREFIX);
	printf("%s Exchange fees collected: $%.0f\n", LOG_PREFIX, total_fees);


	// clean-up after successful execution
	cleanup_fifos(num_traders);
	free_structs(&prods, head, buys, sells);
	free_matches(matches, num_traders, prods.size);
	return 0;

	cleanup:
		// free all allocated memory and return 1 as an error code
		cleanup_fifos(num_traders);
		free_structs(&prods, head, buys, sells);
		free_matches(matches, num_traders, prods.size);
		return 1;
}

void signal_handle(int signum, siginfo_t *info, void *context) {
	if (signum == SIGUSR1) {
		// handle SIGUSR1
		sigusr1 = 1;
		usleep(1);
	} else if (signum == SIGCHLD) {
		// handle SIGCHLD
		sigchld = 1;
		usleep(1);
	}

	pid = info->si_pid;
}

void init_sigaction(struct sigaction *sa) {
	sa->sa_sigaction = signal_handle;
	sigemptyset(&sa->sa_mask);
    sa->sa_flags = SA_SIGINFO;
}

int init_product_list(char products_file[], products *prods) {
	FILE *fp = fopen(products_file, "r");
	if (fp == NULL) {
		return 1;
	}

	// read first line of file
	if (fscanf(fp, "%d\n", &(prods->size)) != 1) {
		fclose(fp);
		return 1;
	}

	char line[PRODUCT_STR_LEN]; // holds a single product string
	int count = 0;
	prods->product_strings = malloc(prods->size * sizeof(char*));
	// read each product string into array
	while (fgets(line, PRODUCT_STR_LEN, fp) != NULL) {
		line[strcspn(line, "\n")] = '\0'; // remove trailing newline

		char *new_line = malloc(strlen(line) + 1);
		strcpy(new_line, line);

		prods->product_strings = realloc(prods->product_strings, (count + 1) * sizeof(char*));
		prods->product_strings[count] = new_line;
		count++;
	}

	// print out resulting list of products to be traded
	printf("%s Trading %d products:", LOG_PREFIX, prods->size);
	for (int i = 0; i < prods->size; i++) {
		printf(" %s", prods->product_strings[i]);
	}
	printf("\n");

	fclose(fp);
	return 0;
}

void init_matches(int ****matches, int num_traders, int prods_size) {
	*matches = (int***)malloc(num_traders * sizeof(int**));
	for (int i = 0; i < num_traders; i++) {
		(*matches)[i] = (int**)calloc(prods_size, sizeof(int*));
		for (int j = 0; j < prods_size; j++) {
			(*matches)[i][j] = (int*)calloc(2, sizeof(int));
		}
	}
}

int spawn_and_communicate(int num_traders, char **argv, trader **head) {
	int trader_id = 0;
	int exchange_path_len = 0;
	int trader_path_len = 0;
	char *exchange_fifo_path = NULL;
	char *trader_fifo_path = NULL;
	trader *prev = *head; // tracks the last trader added to the list
	pid_t forked_pid = -1;
	for (trader_id = 0; trader_id < num_traders; trader_id++) {
		// get the length of each path
		exchange_path_len = snprintf(NULL, 0, FIFO_EXCHANGE, trader_id);
		trader_path_len = snprintf(NULL, 0, FIFO_TRADER, trader_id);

		// allocate memory based on the len we got above
		exchange_fifo_path = malloc(exchange_path_len + 1);
		trader_fifo_path = malloc(trader_path_len + 1);

		// format strings and store in correspondingly labelled areas
		snprintf(exchange_fifo_path, exchange_path_len + 1, FIFO_EXCHANGE, trader_id);
		snprintf(trader_fifo_path, trader_path_len + 1, FIFO_TRADER, trader_id);

		// delete existing fifos
		unlink(exchange_fifo_path);
		unlink(trader_fifo_path);

		// create the fifos and print corresponding creation notification
		int res = mkfifo(exchange_fifo_path, 0666);
		if (res < 0) {
			free(exchange_fifo_path);
			free(trader_fifo_path);
			return 1;
		}
		printf("%s Created FIFO %s\n", LOG_PREFIX, exchange_fifo_path);

		res = mkfifo(trader_fifo_path, 0666);
		if (res < 0) {
			free(exchange_fifo_path);
			free(trader_fifo_path);
			return 1;
		}
		printf("%s Created FIFO %s\n", LOG_PREFIX, trader_fifo_path);

		// fork and exec the trader after creating its fifos
		printf("%s Starting trader %d ", LOG_PREFIX, trader_id);
		printf("(%s)\n", argv[TRADERS_START + trader_id]);
		forked_pid = fork();
		if (forked_pid < 0) {
			return 1;
		} else if (forked_pid == 0) {
			// exec trader binaries from child process
			// first, convert trader ID into a string
			int tid_len = snprintf(NULL, 0, "%d", trader_id);
			char *tid_str = malloc(tid_len + 1); // no need to free
			snprintf(tid_str, tid_len + 1, "%d", trader_id);
			char *args[] = {argv[TRADERS_START + trader_id], tid_str, NULL};
			execv(args[0], args);
			return 1; // should never reach here, so return error code if we do
		}

		// connect to named pipes, create a new trader and add it to list
		trader *new_trader = malloc(sizeof(trader));
		new_trader->fd[1] = open(exchange_fifo_path, O_WRONLY);
		printf("%s Connected to %s\n", LOG_PREFIX, exchange_fifo_path);
		new_trader->fd[0] = open(trader_fifo_path, O_RDONLY);
		printf("%s Connected to %s\n", LOG_PREFIX, trader_fifo_path);
		if (new_trader->fd[0] < 0 || new_trader->fd[1] < 0) {
			return 1;
		}
		// initialize trader data fields
		new_trader->trader_id = trader_id;
		new_trader->process_id = forked_pid;
		new_trader->max_buy_order_id = 0;
		new_trader->max_sell_order_id = 0;
		new_trader->disconnected = 0;

		// add the newly opened trader to the head of the list
		new_trader->next = NULL;
		if (*head == NULL) {
			// empty list -- make new trader the head
			*head = new_trader;
		} else {
			prev->next = new_trader;
		}
		prev = new_trader;

		free(exchange_fifo_path);
		free(trader_fifo_path);
	}

	return 0;
}

int read_and_format_message(trader *curr_trader, char *message_in) {
	int bytes_read = read(curr_trader->fd[0], message_in, BUF_SIZE);
	if (bytes_read < 0) {
		return 1;
	}

	// put the string into proper format
	int delim_index = 0;
    for (int i = 0; i < 256; i++) {
        // look for the ; delimiter
        if (message_in[i] == ';') {
            delim_index = i;
            break;
        } else if (i == 255) {
            // reached end of data without finding delimiter
            return 1;
        }
    }
	message_in[delim_index] = '\0';
	return 0;
}

int determine_cmd_type(char *message_in) {
	// extract the command type from the incomming message
	char type[CMD_LEN];
	int res = sscanf(message_in, "%s", type);
	if (res != 1) {
		return -1;
	}

	// return the corresponding flag
	if (strcmp(type, "BUY") == 0) {
		return BUY;
	} else if (strcmp(type, "SELL") == 0) {
		return SELL;
	} else if (strcmp(type, "AMMEND") == 0) {
		return AMMEND;
	} else if (strcmp(type, "CANECL") == 0) {
		return CANCEL;
	}

	return -1;
}

int execute_command(trader *curr_trader, char *message_in, int cmd_type, products* prods, int *product_index, order ***buys, order ***sells, trader *head) {
	if (curr_trader == NULL) {
		return 1;
	} else if (cmd_type == -1) {
		return 1;
	}

	if (cmd_type == BUY || cmd_type == SELL) {
		// make a new order and add to its respective list
		char cmd[CMD_LEN]; // buy, sell, amend, cancel strings
		char product[PRODUCT_STR_LEN];
		int order_id;
		int quantity;
		int price;
		int res = sscanf(message_in, "%s %d %s %d %d", cmd, &order_id, product, &quantity, &price);
		if (res < 5) {
			return 1;
		}

		// validate order
		*product_index = get_product_index(prods, product);
		if (*product_index == -1) {
			return 1;
		} else if (order_id < OID_MIN || order_id > OID_MAX) {
			return 1;
		} else if (quantity < ORDER_MIN || quantity > ORDER_MAX) {
			return 1;
		} else if (price < ORDER_MIN || price > ORDER_MAX) {
			return 1;
		} else if (cmd_type == BUY && (order_id > curr_trader->max_buy_order_id + 1 || order_id < curr_trader->max_buy_order_id)) {
			// non-consecutive BUY order ID
			return 1;
		} else if (cmd_type == SELL && (order_id > curr_trader->max_sell_order_id + 1 || order_id < curr_trader->max_sell_order_id)) {
			// non-consecutive SELL order ID
			return 1;
		}

		// send appropriate message to all traders
		int msg_len;
		char *msg;
		trader *cursor = head;
		while (cursor != NULL) {
			if (cursor->process_id == curr_trader->process_id && !(curr_trader->disconnected)) {
				// write accepted to trader that made the order
				msg_len = snprintf(NULL, 0 , "ACCEPTED %d;", order_id);
				msg = malloc(msg_len + 1);
				snprintf(msg, msg_len + 1, "ACCEPTED %d;", order_id);
				write(curr_trader->fd[1], msg, strlen(msg));
				free(msg);
			} else if (!(cursor->disconnected)) {
				// let the other traders now about the new order
				if (cmd_type == BUY) {
					// send MARKET BUY
					msg_len = snprintf(NULL, 0, "MARKET BUY %s %d %d;", product, quantity, price);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "MARKET BUY %s %d %d;", product, quantity, price);
					write(cursor->fd[1], msg, strlen(msg));
					free(msg);
				} else if (cmd_type == SELL) {
					// send MARKET SELL
					msg_len = snprintf(NULL, 0, "MARKET SELL %s %d %d;", product, quantity, price);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "MARKET SELL %s %d %d;", product, quantity, price);
					write(cursor->fd[1], msg, strlen(msg));
					free(msg);
				}
			}
			kill(cursor->process_id, SIGUSR1);
			cursor = cursor->next;
		}

		// make the new order
		order *new_order = (order*)malloc(sizeof(order));
		new_order->order_id = order_id;
		new_order->trader_id = curr_trader->trader_id;
		new_order->product = product;
		new_order->product_index = *product_index;
		new_order->quantity = quantity;
		new_order->price = price;

		// update the maximum order ID tracker
		if (cmd_type == BUY) {
			curr_trader->max_buy_order_id++;
		} else if (cmd_type == SELL) {
			curr_trader->max_sell_order_id++;
		}

		// add the order to the corresponding list
		if (cmd_type == BUY) {
			// buy list is sorted in descending order of price
			order *curr = (*buys)[*product_index];
			order *prev = NULL;
			while (curr != NULL && curr->price >= new_order->price) {
				prev = curr;
				curr = curr->next;
			}

			// insert order into its correct position
			if (prev == NULL) {
				// list was empty or it is the highest priced order
				new_order->next = (*buys)[*product_index];
				(*buys)[*product_index] = new_order;
			} else {
				// insert between prev and current
				prev->next = new_order;
				new_order->next = curr;
			}

		} else if (cmd_type == SELL) {
			// sell list is sorted in ascending order of price
			order *curr = (*sells)[*product_index];
			order *prev = NULL;
			while (curr != NULL && curr->price < new_order->price) {
				prev = curr;
				curr = curr->next;
			}

			// insert order into its correct position
			if (prev == NULL) {
				new_order->next = (*sells)[*product_index];
				(*sells)[*product_index] = new_order;
			} else {
				// insert between prev and current
				prev->next = new_order;
				new_order->next = curr;
			}
		}

	} else if (cmd_type == AMMEND) {
		

	} else if (cmd_type == CANCEL) {

	}

	return 0;
}

void display_orderbook(products *prods, order **buys, order **sells) {
	printf("%s\t--ORDERBOOK--\n", LOG_PREFIX);
	for (int i = 0; i < prods->size; i++) {
		printf("%s\tProduct: %s; Buy levels: %d; Sell levels: %d\n", LOG_PREFIX,
				prods->product_strings[i], count_order_levels(buys, i),
				count_order_levels(sells, i));
		display_orders(sells, i, SELL);
		display_orders(buys, i, BUY);
	}
}

int count_order_levels(order **list, int product_index) {
	int count = 0;
	int prev_price = -1;
	order *curr = list[product_index];
	while (curr != NULL) {
		if (curr->price == prev_price) {
			curr = curr->next;
			continue;
		}
		count++;
		prev_price = curr->price;
		curr = curr->next;
	}
	return count;
}

void display_orders(order **list, int product_index, int order_type) {
	order *curr = list[product_index];
	int count = 1;
	int total_qty;
	while (curr != NULL) {
		order *runner = curr->next;
		count = 1;
		total_qty = curr->quantity;
		while (runner != NULL && runner->quantity == curr->quantity && runner->price == curr->price) {
			count++;
			total_qty += runner->quantity;
			runner = runner->next;
		}
		if (count > 1) {
			if (order_type == BUY) {
				printf("%s\t\tBUY %d @ $%d (%d orders)\n", LOG_PREFIX, total_qty, curr->price, count);
			} else if (order_type == SELL) {
				printf("%s\t\tSELL %d @ $%d (%d orders)\n", LOG_PREFIX, total_qty, curr->price, count);
			}
		} else if (count == 1) {
			if (order_type == BUY) {
				printf("%s\t\tBUY %d @ $%d (%d order)\n", LOG_PREFIX, total_qty, curr->price, count);
			} else if (order_type == SELL) {
				printf("%s\t\tSELL %d @ $%d (%d order)\n", LOG_PREFIX, total_qty, curr->price, count);
			}
		}
		curr = runner;
	}
}

void display_positions(trader *head, int ***matches, products *prods) {
	// loop through and print each trader's positions for each product
	printf("%s\t--POSITIONS--\n", LOG_PREFIX);
	trader *curr = head;
	while (curr != NULL) {
		printf("%s\tTrader %d: ", LOG_PREFIX, curr->trader_id);
		for (int i = 0; i < prods->size; i++) {
			printf("%s %d ($%d)", prods->product_strings[i], matches[curr->trader_id][i][0], matches[curr->trader_id][i][1]);
			if (i != prods->size - 1) {
				printf(", ");
			}
		}
		printf("\n");
		curr = curr->next;
	}
}

void find_matches(int ****matches, order ***buys, order ***sells, trader *head, double *total_trading_fees, int product_index) {
	// store the head of the BUY and SELL lists for the most recently added prod
	order *prod_buys = (*buys)[product_index];
	order *prod_sells = (*sells)[product_index];

	if (prod_buys == NULL || prod_sells == NULL) {
		// empty BUY or SELL list for this product
		return;
	}

	// variables used for writing to the traders
	int msg_len;
	char *msg;

	double trading_fee = 0;
	long trading_sum = 0; // tracks the total value of the trade
	while (prod_buys != NULL && prod_sells != NULL) {
		// we match off the top of both lists as long as orders exist
		trading_fee = 0;
		trading_sum = 0;
		if (prod_buys->price >= prod_sells->price) {
			// match if the price of BUY was greater than the price of the SELL

			/*
			 * We have the following 3 cases for a match:
			 	1. The qty to BUY is less than the qty to SELL
					--> Delete BUY, keep SELL
				2. The qty to BUY is equal to the qty to SELL
					--> Delete both BUY and SELL
				3. The qty to BUY is greater than the qty to Sell
					--> Keep BUY, delete SELL
			 */
		
			if (prod_buys->quantity < prod_sells->quantity) {
				// compute price of the trade, this is based on the older order
				trading_sum = prod_buys->price * prod_buys->quantity;

				// compute fee of the trade
				trading_fee = trading_sum * FEE_PERCENTAGE;
				long rounding = (long)(trading_fee + 0.5f);
				trading_fee = (double)(rounding); // rounded to nearest decimal

				// update the total trading fees sum
				*total_trading_fees += trading_fee;

				// reduce the amount of product avaliable for this sell order
				prod_sells->quantity -= prod_buys->quantity;

				// cache the details of the trade
				(*matches)[prod_buys->trader_id][product_index][0] += prod_buys->quantity;
				(*matches)[prod_buys->trader_id][product_index][1] -= trading_sum;
				(*matches)[prod_sells->trader_id][product_index][0] -= prod_buys->quantity;
				(*matches)[prod_sells->trader_id][product_index][1] += (long)(trading_sum - trading_fee);

				// get the traders involved in the match
				trader *buyer = get_trader(-1, prod_buys->trader_id, head);
				trader *seller = get_trader(-1, prod_sells->trader_id, head);

				// print the results of the trade to stdout
				printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%.0f.\n",
				 		LOG_PREFIX, prod_buys->order_id, prod_buys->trader_id, 
						prod_sells->order_id, prod_sells->trader_id, 
						trading_sum, trading_fee);

				// send fill messages to traders involved
				if (!(buyer->disconnected)) {
					// send FILL only if buyer has not disconnected
					msg_len = snprintf(NULL, 0, "FILL %d %d;", prod_buys->order_id, prod_buys->quantity);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "FILL %d %d;", prod_buys->order_id, prod_buys->quantity);
					write(buyer->fd[1], msg, strlen(msg));
					kill(buyer->process_id, SIGUSR1);
					free(msg);
				} 
				
				if (!(seller->disconnected)) {
					// send FILL only if seller has not disconnected
					msg_len = snprintf(NULL, 0, "FILL %d %d;", prod_sells->order_id, prod_buys->quantity);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "FILL %d %d;", prod_sells->order_id, prod_buys->quantity);
					write(seller->fd[1], msg, strlen(msg));
					kill(seller->process_id, SIGUSR1);
					free(msg);
				}

				// remove the BUY order from the list
				order *to_delete = (*buys)[product_index];
				(*buys)[product_index] = ((*buys)[product_index])->next;
				free(to_delete);
				prod_buys = (*buys)[product_index]; // move to the next order

			} else if (prod_buys->quantity == prod_sells->quantity) {
				// compute price of the trade, this is based on the older order
				if (prod_buys->order_id > prod_sells->order_id) {
					trading_sum = prod_sells->price * prod_sells->quantity;
				} else {
					trading_sum = prod_buys->price * prod_sells->quantity;
				}

				// compute fee of the trade
				trading_fee = trading_sum * FEE_PERCENTAGE;
				long rounding = (long)(trading_fee + 0.5f);
				trading_fee = (double)(rounding);

				// update the total trading fees sum
				*total_trading_fees += trading_fee;

				// cache the details of the trade
				(*matches)[prod_buys->trader_id][product_index][0] += prod_buys->quantity;
				(*matches)[prod_buys->trader_id][product_index][1] -= trading_sum;
				(*matches)[prod_sells->trader_id][product_index][0] -= prod_sells->quantity;
				(*matches)[prod_sells->trader_id][product_index][1] += (long)(trading_sum - trading_fee);

				// get the traders involved in the match
				trader *buyer = get_trader(-1, prod_buys->trader_id, head);
				trader *seller = get_trader(-1, prod_sells->trader_id, head);

				// print the results of the trade to stdout
				printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%.0f.\n",
				 		LOG_PREFIX, prod_buys->order_id, prod_buys->trader_id, 
						prod_sells->order_id, prod_sells->trader_id, 
						trading_sum, trading_fee);

				// send fill messages to traders involved
				if (!(buyer->disconnected)) {
					// send FILL only if buyer has not disconnected
					msg_len = snprintf(NULL, 0, "FILL %d %d;", prod_buys->order_id, prod_buys->quantity);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "FILL %d %d;", prod_buys->order_id, prod_buys->quantity);
					write(buyer->fd[1], msg, strlen(msg));
					kill(buyer->process_id, SIGUSR1);
					free(msg);
				}
				
				if (!(seller->disconnected)) {
					// send FILL only if seller has not disconnected
					msg_len = snprintf(NULL, 0, "FILL %d %d;", prod_sells->order_id, prod_sells->quantity);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "FILL %d %d;", prod_sells->order_id, prod_sells->quantity);
					write(seller->fd[1], msg, strlen(msg));
					kill(seller->process_id, SIGUSR1);
					free(msg);
				}

				// remove both orders from their respective lists
				order *to_delete = (*buys)[product_index];
				(*buys)[product_index] = ((*buys)[product_index])->next;
				free(to_delete);
				prod_buys = (*buys)[product_index]; // move to the next order

				to_delete = (*sells)[product_index];
				(*sells)[product_index] = ((*sells)[product_index])->next;
				free(to_delete);
				prod_sells = (*sells)[product_index]; // move to the next order

			} else if (prod_buys->quantity > prod_sells->quantity) {
				// compute price of the trade, this is based on the older order
				if (prod_buys->order_id >= prod_sells->order_id) {
					trading_sum = prod_sells->price * prod_sells->quantity;
				} else {
					trading_sum = prod_buys->price * prod_sells->quantity;
				}

				// reduce the amount of product avaliable for this buy order
				prod_buys->quantity -= prod_sells->quantity;

				// compute fee of the trade
				trading_fee = trading_sum * FEE_PERCENTAGE;
				long rounding = (long)(trading_fee + 0.5f);
				trading_fee = (double)(rounding);

				// update the total trading fees sum
				*total_trading_fees += trading_fee;

				// cache the details of the trade
				(*matches)[prod_buys->trader_id][product_index][0] += prod_sells->quantity;
				// (*matches)[prod_buys->trader_id][product_index][1] -= trading_sum;
				(*matches)[prod_sells->trader_id][product_index][0] -= prod_sells->quantity;
				// (*matches)[prod_sells->trader_id][product_index][1] += (long)(trading_sum - trading_fee);

				// get the traders involved in the match
				trader *buyer = get_trader(-1, prod_buys->trader_id, head);
				trader *seller = get_trader(-1, prod_sells->trader_id, head);

				// if one trader disconnected, connected one pays fees
				if (buyer->disconnected) {
					// charge seller with fees
					(*matches)[prod_buys->trader_id][product_index][1] += (long)(trading_sum - trading_fee);
					(*matches)[prod_sells->trader_id][product_index][1] -= trading_sum;
				} else if (seller->disconnected) {
					(*matches)[prod_buys->trader_id][product_index][1] -=  trading_sum;
					(*matches)[prod_sells->trader_id][product_index][1] += (long)(trading_sum - trading_fee);
				}

				// print the results of the trade to stdout
				if (prod_buys->order_id >= prod_sells->order_id) {
					printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%.0f.\n",
				 		LOG_PREFIX, prod_sells->order_id, prod_sells->trader_id, 
						prod_buys->order_id, prod_buys->trader_id, 
						trading_sum, trading_fee);
				} else {
					printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%.0f.\n",
				 		LOG_PREFIX, prod_buys->order_id, prod_buys->trader_id, 
						prod_sells->order_id, prod_sells->trader_id, 
						trading_sum, trading_fee);
				}

				// send fill messages to traders involved
				if (!(buyer->disconnected)) {
					// send FILL only if buyer has not disconnected
					msg_len = snprintf(NULL, 0, "FILL %d %d;", prod_buys->order_id, prod_sells->quantity);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "FILL %d %d;", prod_buys->order_id, prod_sells->quantity);
					write(buyer->fd[1], msg, strlen(msg));
					kill(buyer->process_id, SIGUSR1);
					free(msg);
				}
				
				if (!(seller->disconnected)) {
					// send FILL only if seller has not disconnected
					msg_len = snprintf(NULL, 0, "FILL %d %d;", prod_sells->order_id, prod_sells->quantity);
					msg = malloc(msg_len + 1);
					snprintf(msg, msg_len + 1, "FILL %d %d;", prod_sells->order_id, prod_sells->quantity);
					write(seller->fd[1], msg, strlen(msg));
					kill(seller->process_id, SIGUSR1);
					free(msg);
				}

				// remove SELL order from the list
				order *to_delete = (*sells)[product_index];
				(*sells)[product_index] = ((*sells)[product_index])->next;
				free(to_delete);
				prod_sells = (*sells)[product_index]; // move to the next order
			}
		} else {
			// no trades possible
			break;
		}
	}
}

trader *get_trader(pid_t pid, int trader_id, trader *head) {
	trader *current = head;
	while (current != NULL) {
		if (current->process_id == pid || current->trader_id == trader_id) {
			return current;
		}
		current = current->next;
	}
	return current;
}

int get_product_index(products *prods, char *product) {
	if (product == NULL) {
		return -1;
	}

	for (int i = 0; i < prods->size; i++) {
		if (strcmp(prods->product_strings[i], product) == 0) {
			return i;
		}
	}

	return -1;
}

void free_structs(products *prods, trader *head, order **buys, order **sells) {
	free_products_list(prods);
	free_trader_list(head);
	free_order_list(buys, prods);
	free_order_list(sells, prods);
}

void free_products_list(products *prods) {
	for (int i = 0; i < prods->size; i++) {
		free(prods->product_strings[i]);
	}
	free(prods->product_strings);
}

void free_trader_list(trader *head) {
	trader *current = head;
	trader *next;
	while (current != NULL) {
		next = current->next;
		free(current); // free the memory used for the trader struct itself
		current = next; // move to next trader in list
	}
}

void free_order_list(order **order_list, products *prods) {
	for (int i = 0; i < prods->size; i++) {
		order *temp;
		while (order_list[i] != NULL) {
			temp = order_list[i];
			order_list[i] = (order_list[i])->next;
			free(temp);
		}
	}
	free(order_list);
}

void free_matches(int ***matches, int num_traders, int prods_size) {
	for (int i = 0; i < num_traders; i++) {
		for (int j = 0; j < prods_size; j++) {
			free(matches[i][j]);
		}
		free(matches[i]);
	}
	free(matches);
}

void cleanup_trader(pid_t pid, trader **head) {
	if (head == NULL) {
		// list was empty
		return;
	}

	// find the trader node with matching pid
	trader *current = *head; // to delete
	trader* previous = NULL;
	while (current != NULL && current->process_id != pid) {
		previous = current;
		current = current->next;
	}

	if (current == NULL) {
		// reached end of list without finding matching pid
		return;
	}

	// clean up trader fifos
	char *fifo_path = NULL;
	int path_len = snprintf(NULL, 0, FIFO_EXCHANGE, current->trader_id);
	fifo_path = malloc(path_len + 1);
	// close and delete exchange fifo
	if (access(fifo_path, F_OK) != -1) {
		close(current->fd[1]);
		unlink(fifo_path);
	}
	free(fifo_path);

	path_len = snprintf(NULL, 0, FIFO_TRADER, current->trader_id);
	fifo_path = malloc(path_len + 1);
	// close and delete trader fifo
	if (access(fifo_path, F_OK) != -1) {
		close(current->fd[0]);
		unlink(fifo_path);
	}
	free(fifo_path);

	printf("%s Trader %d disconnected\n", LOG_PREFIX, current->trader_id);

	// remove trader node from list and free its memory
	if (previous == NULL) {
		*head = current->next;
	} else {
		previous->next = current->next;
	}

	free(current);
}

void cleanup_fifos(int number_of_traders) {
	char *fifo_path = NULL;
	int path_len = 0;
	for (int trader_id = 0; trader_id < number_of_traders; trader_id++) {
		// format the exchange fifo path string
		path_len = snprintf(NULL, 0, FIFO_EXCHANGE, trader_id);
		fifo_path = malloc(path_len + 1);
		snprintf(fifo_path, path_len + 1, FIFO_EXCHANGE, trader_id);

		if (access(fifo_path, F_OK) != -1) {
			// close and delete exchange fifos
			// close() once opened
			unlink(fifo_path);
		}

		free(fifo_path);

		// format the trader fifo path string
		path_len = snprintf(NULL, 0, FIFO_TRADER, trader_id);
		fifo_path = malloc(path_len + 1);
		snprintf(fifo_path, path_len + 1, FIFO_TRADER, trader_id);

		if (access(fifo_path, F_OK) != -1) {
			// close and delete trader fifos
			unlink(fifo_path);
		}

		free(fifo_path);
	}
}