/**
 * comp2017 - assignment 3
 * Matthew Lazar
 * mlaz7837
 * 490431454
 */

#include "pe_exchange.h"

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Invalid number of arguments provided.\n");
		return 1;
	}


	int res; // stores result of init functions for error checking

	printf("%s Starting\n", LOG_PREFIX);

	// initialize structs and prepare for exchange launch
	products prods;
	res = initialize_product_list(argv[1], &prods);
	if (res) {
		printf("Error initializing products list using file %s.\n", argv[1]);
		goto cleanup;
	}

	res = spawn_and_communicate(argc - TRADERS_START, argv);
	if (res) {
		printf("Error: %s\n", strerror(errno));
		goto cleanup;
	}


	// clean-up after successful execution
	cleanup_fifos(argc - TRADERS_START);
	free_structs(&prods);
	// free_strings(exchange_fifo_path, trader_fifo_path);
	return 0;

	cleanup:
		// free all allocated memory and return 1 as an error code
		cleanup_fifos(argc - TRADERS_START);
		free_structs(&prods);
		// free_strings(exchange_fifo_path, trader_fifo_path);
		return 1;
}

int initialize_product_list(char products_file[], products *prods) {
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

int spawn_and_communicate(int num_of_traders, char **argv) {
	int trader_id = 0;
	int exchange_path_len = 0;
	int trader_path_len = 0;
	char *exchange_fifo_path = NULL;
	char *trader_fifo_path = NULL;
	pid_t pid = -1;
	for (trader_id = 0; trader_id < num_of_traders; trader_id++) {
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
			return 1;
		}
		printf("%s Created FIFO %s\n", LOG_PREFIX, exchange_fifo_path);

		res = mkfifo(trader_fifo_path, 0666);
		if (res < 0) {
			return 1;
		}
		printf("%s Created FIFO %s\n", LOG_PREFIX, trader_fifo_path);

		// fork and exec the trader after creating its fifos
		printf("%s Starting trader %d ", LOG_PREFIX, trader_id);
		printf("./bin/%s\n", argv[TRADERS_START + trader_id]);
		pid = fork();
		if (pid < 0) {
			return 1;
		} else if (pid == 0) {
			// exec trader binaries from child process
			// first, convert trader ID into a string
			int tid_len = snprintf(NULL, 0, "%d", trader_id);
			char *tid_str = malloc(tid_len + 1);
			snprintf(tid_str, tid_len + 1, "%d", trader_id);
			char *args[] = {argv[TRADERS_START + trader_id], tid_str, NULL};
			execv(args[0], args);
			return 1; // should never reach here, so return error code if we do
		}


		free(exchange_fifo_path);
		free(trader_fifo_path);
	}

	return 0;
}

void free_structs(products *prods) {
	free_products_list(prods);
}

void free_products_list(products *prods) {
	for (int i = 0; i < prods->size; i++) {
		free(prods->product_strings[i]);
	}
	free(prods->product_strings);
}

void free_strings(char *exchange_fifo_path, char *trader_fifo_path) {
	free(exchange_fifo_path);
	free(trader_fifo_path);
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