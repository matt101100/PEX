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

	// initialize structs and prepare for exchange launch
	int res; // stores result of init functions for error checking

	products products;
	res = initialize_product_list(argv[1], &products);
	if (res) {
		printf("Error initializing products list using file %s.\n", argv[1]);
		goto cleanup;
	}

	printf("%s Starting\n", LOG_PREFIX);
	// todo

	// clean-up after successful execution
	free_structs(&products);
	return 0;

	cleanup:
		// free all allocated memory and return 1 as an error code
		free_structs(&products);
		return 1;
}

int initialize_product_list(char products_file[], products *prods) {
	FILE *fp = fopen(products_file, "r");
	if (fp == NULL) {
		return 1;
	}

	fscanf(fp, "%d", &(prods->size)); // read first line of file
	char line[PRODUCT_STR_LEN]; // holds a single product string
	int count = 0;
	// read each product string into array
	prods->product_strings = NULL;
	while (fgets(line, PRODUCT_STR_LEN, fp) != NULL) {
		line[strcspn(line, "\n")] = '\0'; // remove trailing newline

		char *new_line = malloc(strlen(line) + 1);
		strcpy(new_line, line);

		prods->product_strings = realloc(prods->product_strings, (count + 1) * sizeof(char*));
		prods->product_strings[count] = new_line;
		count++;
	}

	return 0;
}

void free_structs(products *prods) {
	free_products_list(prods);
}

void free_products_list(products *prods) {
	for (int i = 0; i < prods->size + 1; i++) { // idk why i have to +1 here
		free(prods->product_strings[i]);
	}
	free(prods->product_strings);
}