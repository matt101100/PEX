# Overview
PEX is a simple market exchange simulator written in C, using interprocess communication (IPC). It initializes a signal handler and product lists, then spawns trader processes that interact via FIFOs. An event loop processes trader commands (e.g., buy, sell, cancel), matching orders and managing order books. The program handles signals to track trader activity and exits once all traders disconnect. Designed to be fault-tolerant, it minimizes disruption from asynchronous events using a short signal handler.

# Building and Running
A Makefile is provided to make building and running the program simple. To compile the program, us
```
$ make
```
You can then use 
```
$ make run trader_file1 trader_file2 ...
```
This runs the program with the supplied trader binaries. For example, to use ```pe_trader.c``` to test the program, you can use 
```
$ make run pe_trader
```
