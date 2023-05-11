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
	int total_sales = 0;

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

	// order lists
	order **buys = (order**)calloc(prods.size, sizeof(order*));
	order **sells = (order**)calloc(prods.size, sizeof(order*));

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
	int trader_disconnect = 0; // counts number of traders disconnected
	int cmd_type = -1;
	char *message_in = NULL;
	trader *curr_trader = NULL; // tracks the last trader that signalled
	while (trader_disconnect < num_traders) {
		while (!sigchld && !sigusr1) {
			// wait for either signal
			pause();
		}
		if (sigusr1) {
			sigusr1 = 0; // reset flag

			// parse input of trader that sent sigusr1 and return corresponding output
			curr_trader = get_trader(pid, -1, head);
			message_in = read_and_format_message(curr_trader);
			printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, curr_trader->trader_id, message_in);
			cmd_type = determine_cmd_type(message_in);
			if (cmd_type == -1) {
				// notify trader of invalid message
				write(curr_trader->fd[1], "INVALID;", strlen("INVALID;"));
				kill(curr_trader->process_id, SIGUSR1);
				continue;
			}

			res = execute_command(curr_trader, message_in, cmd_type, &prods, &buys, &sells);
			display_orderbook(&prods, buys, sells);
			

		} else if (sigchld) {
			sigchld = 0; // reset flag

			// perform disconnection and cleanup of terminated trader
			cleanup_trader(pid, &head);
			trader_disconnect++;
		}
	}

	printf("%s Trading completed\n", LOG_PREFIX);
	printf("%s Exchange fees collected: $%d\n", LOG_PREFIX, (int)FEE_PERCENTAGE * total_sales);


	// clean-up after successful execution
	cleanup_fifos(num_traders);
	free_structs(&prods, head, buys, sells);
	free(message_in);
	return 0;

	cleanup:
		// free all allocated memory and return 1 as an error code
		cleanup_fifos(num_traders);
		free_structs(&prods, head, buys, sells);
		return 1;
}

void signal_handle(int signum, siginfo_t *info, void *context) {
	if (signum == SIGUSR1) {
		// handle SIGUSR1
		sigusr1 = 1;
	} else if (signum == SIGCHLD) {
		// handle SIGCHLD
		sigchld = 1;
	} else if (signum == SIGPIPE) {
		printf("here\n");
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

int spawn_and_communicate(int num_traders, char **argv, trader **head) {
	int trader_id = 0;
	int exchange_path_len = 0;
	int trader_path_len = 0;
	char *exchange_fifo_path = NULL;
	char *trader_fifo_path = NULL;
	pid_t pid = -1;
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
		pid = fork();
		if (pid < 0) {
			return 1;
		} else if (pid == 0) {
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
		new_trader->process_id = pid;

		// add the newly opened trader to the head of the list
		new_trader->next = *head;
		*head = new_trader;

		free(exchange_fifo_path);
		free(trader_fifo_path);
	}

	return 0;
}

char *read_and_format_message(trader *curr_trader) {
	if (curr_trader == NULL) {
		return NULL;
	}

	char *buffer = malloc(BUF_SIZE);
	if (buffer == NULL) {
		free(buffer);
		return NULL;
	}

	int size = BUF_SIZE;
	int position = 0;
	int bytes_read = 0;
	do {
		bytes_read = read(curr_trader->fd[0], buffer + position, 1);
		if (bytes_read == -1) {
			free(buffer);
			return NULL;
		}

		position += bytes_read;
		if (position == size) {
			size *= 2;
			char *new_buf = realloc(buffer, size);
			if (new_buf == NULL) {
				free(new_buf);
				return NULL;
			}
			buffer = new_buf;
		}
	} while (bytes_read > 0);

	// put the string into proper format
	int delim_index = 0;
    for (int i = 0; i < position; i++) {
        // look for the ; delimiter
        if (buffer[i] == ';') {
            delim_index = i;
            break;
        } else if (i == bytes_read - 1) {
            // reached end of data without finding delimiter
            return NULL;
        }
    }
	
	char *formatted_string = malloc(delim_index + 1);
	if (formatted_string == NULL) {
		free(formatted_string);
		free(buffer);
		return NULL;
	}

	memcpy(formatted_string, buffer, delim_index + 1);
	formatted_string[delim_index] = '\0';
	
	free(buffer);

	return formatted_string;
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

int execute_command(trader *curr_trader, char *message_in, int cmd_type, products* prods, order ***buys, order ***sells) {
	if (curr_trader == NULL) {
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
		int product_index = get_product_index(prods, product);
		if (product_index == -1) {
			return 1;
		} else if (order_id < OID_MIN || order_id > OID_MAX) {
			return 1;
		} else if (quantity < ORDER_MIN || quantity > ORDER_MAX) {
			return 1;
		} else if (price < ORDER_MIN || price > ORDER_MAX) {
			return 1;
		}

		// notify the trader that its order was accepted
		int msg_len = snprintf(NULL, 0, "ACCEPTED %d;", order_id);
		char *accepted_msg = malloc(msg_len + 1);
		if (accepted_msg == NULL) {
			return 1;
		}
		snprintf(accepted_msg, msg_len + 1, "ACCEPTED %d;", order_id);
		write(curr_trader->fd[1], accepted_msg, strlen(accepted_msg));
		kill(curr_trader->process_id, SIGUSR1);
		free(accepted_msg);

		// make the new order
		order *new_order = (order*)malloc(sizeof(order));
		new_order->order_id = order_id;
		new_order->product = product;
		new_order->product_index = product_index;
		new_order->quantity = quantity;
		new_order->price = price;

		// add the order to the corresponding list
		if (cmd_type == BUY) {
			// buy list is sorted in descending order of price
			order *curr = (*buys)[product_index];
			order *prev = NULL;
			while (curr != NULL && curr->price >= new_order->price) {
				prev = curr;
				curr = curr->next;
			}

			// insert order into its correct position
			if (prev == NULL) {
				// list was empty or it is the highest priced order
				new_order->next = (*buys)[product_index];
				(*buys)[product_index] = new_order;
			} else {
				// insert between prev and current
				prev->next = new_order;
				new_order->next = curr;
			}

		} else if (cmd_type == SELL) {
			// sell list is sorted in ascending order of price
			order *curr = (*sells)[product_index];
			order *prev = NULL;
			while (curr != NULL && curr->price < new_order->price) {
				prev = curr;
				curr = curr->next;
			}

			// insert order into its correct position
			if (prev == NULL) {
				new_order->next = (*sells)[product_index];
				(*sells)[product_index] = new_order;
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
		display_orders(buys, i, BUY);
		display_orders(sells, i, SELL);
	}
}

int count_order_levels(order **list, int product_index) {
	int count = 0;
	order *curr = list[product_index];
	while (curr != NULL) {
		count++;
		curr = curr->next;
	}
	return count;
}

void display_orders(order **list, int product_index, int order_type) {
	char *order_prefix = malloc(strlen("SELL") + 1);
	if (order_type == BUY) {
		strcpy(order_prefix, "BUY");
	} else if (order_type == SELL) {
		strcpy(order_prefix, "SELL");
	}
	order *curr = list[product_index];
	int count = 1;
	while (curr != NULL) {
		order *runner = curr->next;
		while (runner != NULL) {
			if (runner->quantity == curr->quantity && runner->price == curr->price) {
				count++;
			} else {
				break;
			}
		}
		if (count > 1) {
			printf("%s\t\t%s %d @ $%d (%d orders)\n", LOG_PREFIX, order_prefix, curr->quantity, curr->price, count);
		} else if (count == 1) {
			printf("%s\t\t%s %d @ $%d (%d order)\n", LOG_PREFIX, order_prefix, curr->quantity, curr->price, count);
		}
		curr = runner;
	}
	free(order_prefix);
}

void display_positions(trader *head) {
	printf("%s\t--POSITIONS--\n", LOG_PREFIX);

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