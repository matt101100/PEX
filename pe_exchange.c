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
	}

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

	sleep(3);

	// event loop
	int trader_disconnect = 0; // counts number of traders disconnected
	while (trader_disconnect < num_traders) {
		while (!sigchld || !sigusr1) {
			// wait for either signal
			pause();
		}

		if (sigusr1) {
			// parse input of trader that sent sigusr1 and return corresponding output

		} else if (sigchld) {
			// perform disconnection and cleanup of terminated trader
			cleanup_trader(pid, &head);
			trader_disconnect++;
		}
	}

	// clean-up after successful execution
	cleanup_fifos(num_traders);
	free_structs(&prods, head);
	return 0;

	cleanup:
		// free all allocated memory and return 1 as an error code
		cleanup_fifos(num_traders);
		free_structs(&prods, head);
		return 1;
}

void signal_handle(int signum, siginfo_t *info, void *context) {
	if (signum == SIGUSR1) {
		// handle SIGUSR1
		sigusr1 = 1;
	} else if (signum == SIGCHLD) {
		// handle SIGCHLD
		sigchld = 1;
		pid = info->si_pid;
	}
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
		new_trader->buy_orders = NULL; // no buy or sell orders upon init
		new_trader->sell_orders = NULL;
		new_trader->min_sell = NULL;
		new_trader->max_buy = NULL;

		// add the newly opened trader to the head of the list
		new_trader->next = *head;
		*head = new_trader;

		free(exchange_fifo_path);
		free(trader_fifo_path);
	}

	return 0;
}

void free_structs(products *prods, trader *head) {
	free_products_list(prods);
	free_trader_list(head);
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
		
		// free the dynamically allocated trader fields
		if (current->buy_orders != NULL) {
			free(current->buy_orders);
		}
		if (current->sell_orders != NULL) {
			free(current->sell_orders);
		}
		if (current->min_sell != NULL) {
			free(current->min_sell);
		}
		if (current->max_buy != NULL) {
			free(current->max_buy);
		}
		free(current); // free the memory used for the trader struct itself

		current = next; // move to next trader in list
	}
}

void cleanup_trader(pid_t pid, trader **head) {
	if (head == NULL) {
		// list was empty
		return;
	}

	// find the trader node with matching pid
	trader *current = *head; // to delete
	trader* previous;
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

	printf("%s Trader %d disconnected\n", LOG_PREFIX, current->trader_id);

	// remove trader node from list and free its memory
	if (previous == NULL) {
		*head = current->next;
	} else {
		previous->next = current->next;
	}

	free(current->buy_orders);
	free(current->sell_orders);
	free(current->min_sell);
	free(current->max_buy);
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