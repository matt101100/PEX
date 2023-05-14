#include "pe_trader.h"

volatile sig_atomic_t sigusr1 = 0; // flag set when sigusr1 sent from exchange

int read_fd;
int write_fd;
int order_id;

// global buffers
char message_in[MESSAGE_LEN]; // stores the incomming message from exchange
char read_filepath[FILEPATH_LEN];
char write_filepath[FILEPATH_LEN];

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    // register signal handler
    struct sigaction sa = initialize_signal_action();
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        printf("Sigaction error.\n");
        return 1;
    }

    // get trader ID
    int trader_id = atoi(argv[1]);

    // connect to named pipes
    read_fd = connect_to_named_pipe(READ, trader_id); // exchange writes
    write_fd = connect_to_named_pipe(WRITE, trader_id); // trader writes

    if (read_fd == -1) {
        printf("Failed to connect to read pipe.\n");
        return 1;
    } else if (write_fd == -1) {
        printf("Failed to connect to write pipe.\n");
        return 1;
    }

    // event loop:
    while (1) {
        // Wait for SIGUSR1 from parent process
        while (!sigusr1) {
            pause();
        }

        sigusr1 = 0; // reset the flag

        int res = format_order(write_fd, message_in);
        if (res == 1) {
            // invalid order
            break;
        } else if (res == 2) {
            // do nothing after getting ACCEPTED or FILL
            continue;
        }

        // Signal parent process that buy order has been sent
        while (!sigusr1) {
            kill(getppid(), SIGUSR1);
            sleep(5);
        }
    }

    // clear buffers and delete fifos
    fsync(read_fd);
    fsync(write_fd);
    close(read_fd);
    close(write_fd);
    unlink(read_filepath);
    unlink(write_filepath);
}

void sigusr1_handle(int signum) {
    read_exchange_msg(read_fd);
    if (strcmp(message_in, "MARKET OPEN;") == 0) {
        // MARKET OPEN but can't read yet
        sigusr1 = 0;
    } else {
        // allow reading from named pipes if a SIGUSR1 sent after MARKET OPEN
        sigusr1 = 1;
    }
}

struct sigaction initialize_signal_action(void) {
    struct sigaction sa;

    sa.sa_handler = sigusr1_handle;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    return sa;
}

int connect_to_named_pipe(int connection_type, int trader_id) {
    int fd = -1;
    if (connection_type == READ) {
        // format filepath string
        snprintf(read_filepath, FILEPATH_LEN, FIFO_EXCHANGE, trader_id);

        // connect as RDONLY
        fd = open(read_filepath, O_RDONLY);

    } else if (connection_type == WRITE) {
        snprintf(write_filepath, FILEPATH_LEN, FIFO_TRADER, trader_id);

        // connect as WRONLY
        fd = open(write_filepath, O_WRONLY);
    }

    return fd;
}

int read_exchange_msg(int read_fd) {
    // read the message into buffer
    int bytes_read = read(read_fd, message_in, MESSAGE_LEN);
    if (bytes_read == -1) {
        return -1;
    }

    // format the message string
    int delim_index = 0;
    for (int i = 0; i < bytes_read; i++) {
        // look for the ; delimiter
        if (message_in[i] == ';') {
            delim_index = i;
            break;
        } else if (i == bytes_read - 1) {
            // reached end of data without finding delimiter
            return -1;
        }
    }
    // null-terminate the string
    message_in[delim_index + 1] = '\0';

    return 0;
}

int format_order(int write_fd, char sell_order[]) {
    // set up variables and buffers to perform order validation on
    char start[MESSAGE_LEN]; // holds start of message
    char order_type[MESSAGE_LEN];
    char product[PRODUCT_STR_LEN];
    int quantity;
    int price;

    // fill vars and buffers with corresponding values
    int res = sscanf(message_in, "%s %s %s %d %d", start, order_type, product,
                     &quantity, &price);

    // nothing to do upon receiving an ACCEPTED or FILL message
    if (strcmp(start, "ACCEPTED") == 0) {
        return 2;
    } else if (strcmp(start, "FILL") == 0) {
        return 2;
    }

    if (res < 5) {
        // less than expected strings and nums read in
        return 1;
    } else if (strcmp(start, "MARKET") != 0) {
        // invalid message start
        return 1;
    }

    // validate order and format corresponding order
    if (strcmp(order_type, "SELL") != 0) {
        // ignore buy orders for now
        return 2;
    }

    if (quantity >= 1000) {
        // can't be 1000 or more of anything, regardless of price
        return 1;
    }

    // populate order string
    char order[MESSAGE_LEN];
    snprintf(order, MESSAGE_LEN, BUY, order_id++, product, quantity, price);

    // write message to exchange
   int bytes_read = write_to_exchange(write_fd, order);
   if (bytes_read == -1) {
    return 1;
   }

   return 0;
}

int write_to_exchange(int write_fd, char to_write[]) {
    int bytes_written = write(write_fd, to_write, strlen(to_write));
    if (bytes_written == -1) {
        return -1;
    }
    return bytes_written;
}