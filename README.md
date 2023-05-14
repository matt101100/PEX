1. Describe how your exchange works.

First initialises the signal handler, and product list struct. Spawn_and_communicate() goes through all trader binaries,
makes their FIFOs, forks + execs the binary, connects to FIFOs and initialises a unique trader struct for all traders. Buy, sell and match lists are initialised and memory is allocated for them. Market open is sent to all traders then event loop begins. Event loop works as follows: 
    1. wait for either SIGUSR1 or SIGCHLD using pause
    2. If SIGUSR1, get trader that sent signal --> read and format message from corresponding FIFO --> act on command (make order, amend, cancel) --> try find matches --> print orderbook --> print positions --> wait for signal.
    3. If SIGCHLD, get trader that sent signal --> mark it as disconnected --> display message --> wait for signal.
After all traders disconnect, print termination messages, clean up allocated memory, end program.
Any issues with initialising structs / running traders results in immediate termination of program.

2. Describe your design decisions for the trader and how it's fault-tolerant.
Short signal handler that just sets a flag ensures that asynchronous events do not interrupt the main program flow
for too long. This allows the main event loop to finish dealing with messages as directly and quickly as possible to ensure
that incomming signals are not left waiting for too long and are seen and dealt with in a reasonable amount of time.
Furthermore, to ensure that signals from the trader are not lost by the exchange, signals are sent periodically to ensure the
exchange sees at least one of them and can handle it appropriately. 

3. Describe your tests and how to run them.
N/a.