# README for ledyard.c

### Ledyard Bridge Construction Zone

This program simulates the potential traffic conditions the Ledyard Bridge will face in 2021 as it accomodates for its repairs. During this time, the bridge will be one-way, and can only hold MAX_CARS (currently set to 3) at a time. The program allows the user to simulate these conditions with a user-chosen (or randomly generated) number of cars and their directions (TO_HANOVER or TO_NORWICH).

### Usage

To build, simply run `make`.

To run the program:

```bash
./ledyard
```

To clean up, simply run `make clean`.

### Notes

The purpose of this project is to practice using synchronization of multiple threads to solve concurrency problems.

The user does not have complete control over the simulation -- only the number of cars and each one's directions. Randomness (of variable `sleep()` time) is invoked to encourage potential interleavings, including between each creation of a car thread, before each car arrives to the bridge, after each car gets on the bridge, and before each car exits. Thus, the lifetime of a single car thread may be extended by as little as 0 seconds, and as much as 11 seconds.

I implemented a car struct that holds several variables and ptrs that consolidate everything that the car would need to read/write to in its lifetime. With this, I am assuming that the Ledyard Bridge struct's variables' addresses will not (and cannot) change or be changed by a "critical section", thus being safe from any dangerous race conditions.

### Testing

I tested four simulations of the `ledyard` program in order to get the most range of input and variable interleaving conditions as possible. The output of these four simulations were saved to file `testing.txt`.

 - The first simulation was user-controlled; testing was done to test correct validation of user input.
 - The remaining three simulations were randomly-generated so as to show/test as many different bridge states and conditions as possible

The command used to print input and ouput was:
```bash
script -c ./ledyard testing.txt
```

