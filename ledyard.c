/* Author: Erich Woo
 * Date: 29 September 2020
 * Purpose: To simulate the Ledyard Bridge Construction Zone problem coming
 * in 2021, where bridge traffic will be one-way with a maximum number of cars
 * utilizing multiple threads, each representing a car approaching the bridge
 */

#include <pthread.h>
#include <stdio.h>  // for printf
#include <unistd.h> // for sleep()
#include <stdlib.h> // for rand()
#include <sys/time.h> // for time of day random seeding
#include <limits.h> // for UINT_MAX
#include <string.h> // for strlen()
#include <ctype.h> // for isspace()

#define MAX_CARS 3      // maximum number of cars on Ledyard at a time
#define NO_DIRECTION -1 
#define TO_HANOVER 0
#define TO_NORWICH 1
#define STR_LEN 10

/*************************** DATA STRUCTURES **************************/

// define a data structure for the state of the bridge
typedef struct bridge_state {
  char* str_dir;    // current direction of cars as str for ease in printing
  int dir;          // current direction of cars as int
  int num_cars;     // number of cars currently on bridge
  int wait_hanover; // number of cars waiting to go to Hanover 
  int wait_norwich; // number of cars waiting to go to Norwich
  pthread_mutex_t lock; // Mutex Lock for reading/writing bridge_state
  pthread_cond_t want_to_hanover; // Cond Var for cars going to Hanover
  pthread_cond_t want_to_norwich; // Cond Var for cars going to Norwich
} bridge_state_t;

// define a data structue for the car
// and all the bridge variable addresses it will eventually write to
// the ptrs are helpful for identifying which variables in bridge to edit
typedef struct car {
  char* str_dir;          // dir as a string for ease in printing
  int dir;                // intended direction
  int other_dir;          // opposite of car's direction
  int* wait_dir;          // ptr to bridge's 'wait_town' in current dir
  int* wait_other_dir;    // ptr to bridge's 'wait_town' in other dir
  pthread_cond_t* current;// ptr to the applicable cond var in bridge for this car's direction
  pthread_cond_t* other;  // ptr to the other cond var
} car_t;

/********************* GLOBALS *******************/

static bridge_state_t ledyard; // global variable for the ledyard bridge state

/********************** HELPER FUNCTIONS ********************/

/* Helper function for a possible sleep to encourage
 * interleavings. Utilizes rand() to generate randomness
 * 50/50 chance that function will sleep(). If sleep()
 * is invoked, the duration will be a random number in 
 * a range chosen by the caller, INCLUSIVE
 *
 * @param min the min sleep time if interleaving is coinflipped
 * @param max the max sleep time if interleaving is coinflipped
 */
static void consider_interleaving(int min, int max) {
  int time;
  int bool = rand() % 2; // 0 or 1
  int range = max - min + 1;
  if (bool) {
    time = (rand() % range) + min; 
    sleep(time);
  }
}

/* Returns the minimum of two integers
 * Helper function for executing the right number
 * of cond signals. If a and b are equal
 * returns value of b (same as a).
 *
 * @param a the first int
 * @param b the second int
 * @return the minimum value between the two ints
 */
static int min(int a, int b) {
  return a < b ? a : b;
}

/* must declare fileno() */
int fileno(FILE *stream);

/* Prompt the user with message, and save input at buffer
 * (which should have space for at least len bytes).
 * Function is safe from leftover chars on input
 *
 * Function borrowed from Professor Sean Smith and Adem Salem from CS58
 * with a few details edited for ledyard program functionality
 *
 * @param message the message to prompt the user with
 * @param buffer where to save input to
 * @param len the length of string to save
 * @return 0 on success, -1 on error or char overflow
 */
static int input_string(char *message, char *buffer, int len) {
  int rc = 0, fetched, lastchar;

  if (NULL == buffer)
    return -1;

  if (message) {
    if (isatty(fileno(stdin))) // don't print prompt if stdin
      printf("%s", message);   // isn't keyboard; for testing cleanliness
  }
  // get the string.  fgets takes in at most 1 character less than
  // the second parameter, in order to leave room for the terminating null.  
  // See the man page for fgets.
  fgets(buffer, len, stdin);

  fetched = strlen(buffer);
  
  // warn the user if we may have left extra chars
  if ( (fetched + 1) >= len) {
    // flush stdin
    // https://stackoverflow.com/questions/7898215/how-to-clear-input-buffer-in-c
    int c;
    while (buffer[fetched - 1] != '\n' && ((c = getchar()) != '\n' && c != EOF)) {
      if (isspace(c) == 0) // if the next char isn't space, have error return
	rc = -1;
    }
  }
  // consume a trailing newline
  if (fetched) {
    lastchar = fetched - 1;
    if ('\n' == buffer[lastchar])
      buffer[lastchar] = '\0';
  }

  return rc;
}

/*********************** THREAD-INVOKED FUNCTIONS ***********************/

/* Initializes and assigns a car struct's variables 
 * to the appropriate directions and ledyard addresses 
 * that the car will eventually edit in functions below.
 *
 * Note: car->str_dir is dynamically allocated in the function,
 * and must be later free'd by the caller.
 *
 * @param car a pointer to the car struct
 * @param dir the car's direction
 * @return 0 on success, -1 if dir isn't TO_HANOVER or TO_NORWICH
 */
static int initialize_car(car_t* car, int dir) {
  car->str_dir = (char*) malloc((strlen("Hanover") + 1) * sizeof(char));
  if (dir == TO_HANOVER) {
    strcpy(car->str_dir,"Hanover");
    car->dir = TO_HANOVER;
    car->other_dir = TO_NORWICH;
    car->wait_dir = &ledyard.wait_hanover;
    car->wait_other_dir = &ledyard.wait_norwich;
    car->current = &ledyard.want_to_hanover;
    car->other = &ledyard.want_to_norwich;
  }
  else if (dir == TO_NORWICH) {
    strcpy(car->str_dir,"Norwich");
    car->dir = TO_NORWICH;
    car->other_dir = TO_HANOVER;
    car->wait_dir = &ledyard.wait_norwich;
    car->wait_other_dir = &ledyard.wait_hanover;
    car->current = &ledyard.want_to_norwich;
    car->other = &ledyard.want_to_hanover;
  }
  else {
    fprintf(stderr, "Arriving car has no intended direction\n");
    return -1;
  }
  
  return 0;
}

/* Handles a car arriving at the bridge with a direction, editing
 * the bridge state to accomodate the newly arriving car.
 *
 * The car can only get on the bridge once these conditions are true: 
 * (1) ledyard's traffic is opposite the car's direction
 * (2) ledyard is at max capacity
 * If not, car waits for a signal when these conditions are made 
 * true before getting on the bridge (of course Mesa-style)
 *
 * This function is a critical section, and thus utilizes 
 * the bridge's mutex over the entire function. It will release
 * when it is waiting for favorable conditions stated below.
 *
 * @param car a pointer to the arriving car 
 * @return 0 on success, -1 on mutex error or flawed invariant
 */
static int arrive_bridge(car_t* car) {  
  if (pthread_mutex_lock(&ledyard.lock)) {
    fprintf(stderr, "Error acquiring lock for arrive_bridge()");
    return -1;
  }
  /************** Waiting Lobby ****************/
  (*car->wait_dir)++;    // add car to waiting lobby
  printf("A new car is waiting to go to %s\n", car->str_dir);

  // wait until conditions are true
  while (ledyard.dir == car->other_dir || ledyard.num_cars >= MAX_CARS) {
    if (pthread_cond_wait(car->current, &ledyard.lock)) {
      fprintf(stderr, "Error blocking thread on a condition variable\n");
      return -1;
    }
  }
  
  /**************** Getting on the Bridge **************/    
  // error checking before editing bridge state
  if (ledyard.dir == car->other_dir) {
    fprintf(stderr, "KABOOOM! You just caused a car crash!\n");
    return -1;
  }
  if (ledyard.num_cars >= MAX_CARS) {
    fprintf(stderr, "KERSPLASH! Your bridge just collapsed from over-capacity!\n");
    return -1;
  }

  // adding a new car to the bridge state
  if (ledyard.dir == NO_DIRECTION) {
    // handle broken invariant
    if (ledyard.num_cars != 0) {
      fprintf(stderr, "Error; bridge in invalid state, having no direction with %d car(s) on it\n", ledyard.num_cars);
      return -1;
    }
    // reassign the new direction if needed
    ledyard.dir = car->dir;          
    strcpy(ledyard.str_dir, car->str_dir);
  }
  (*car->wait_dir)--;    // remove car from waiting lobby
  ledyard.num_cars++;    // add car to bridge

  printf("+++ A car got on bridge to %s +++\n", car->str_dir);

  if (pthread_mutex_unlock(&ledyard.lock)) {
    fprintf(stderr, "Error releasing lock for arrive_bridge()\n");
    return -1;
  }

  return 0;
}

/* Prints the bridge's direction, number of cars, and the waiting cars
 * to the user. This function is called when a new car has gotten on 
 * the bridge aka after arrive_bridge returns. 
 * 
 * This function is a critical section, and thus utilizes 
 * the bridge's mutex over the entire function.
 *
 * @param car a pointer to the car on the bridge
 * @return 0 on success, -1 on mutex lock/unlock error
 */
static int on_bridge(car_t* car) {
  if (pthread_mutex_lock(&ledyard.lock)) {
    fprintf(stderr, "Error acquiring lock for on_bridge()\n");
    return -1;
  }

  printf("\n====== Ledyard Bridge ======\n");
  printf("Flow of Traffic: %d cars to %s\n", ledyard.num_cars, ledyard.str_dir);
  printf("Cars waiting for Hanover: %d\n", ledyard.wait_hanover);
  printf("Cars waiting for Norwich: %d\n\n", ledyard.wait_norwich);

  if (pthread_mutex_unlock(&ledyard.lock)) {
    fprintf(stderr, "Error releasing lock for on_bridge()\n");
    return -1;
  }

  return 0;
}

/* Handles a car exiting the bridge, editing the bridge state
 * once the car leaves and sending signal(s) to the appropriate
 * cars
 *
 * This function is a critical section, and thus utilizes 
 * the bridge's mutex over the entire function.
 *
 * @param car a pointer to the exiting car
 * @return 0 on success, -1 on error
 */
static int exit_bridge(car_t* car) {
  int num_sig_current = 0; // # of signals to send in the current dir
  int num_sig_other = 0; // # of signals to send in other dir

  if (pthread_mutex_lock(&ledyard.lock)) {
    fprintf(stderr, "Error acquiring lock for exit_bridge()\n");
    return -1;
  }

  ledyard.num_cars--;   // removing car from bridge state
  // editing bridge state if no more cars on bridge
  if (ledyard.num_cars == 0) {
    ledyard.dir = NO_DIRECTION;
    strcpy(ledyard.str_dir, "Neither");
    num_sig_other = min(MAX_CARS, *car->wait_other_dir);
  }
  num_sig_current = min(MAX_CARS - ledyard.num_cars, *car->wait_dir);

  // signal current direction's waiting cars
  int i;
  for (i = 0; i < num_sig_current; i++) {
    if (pthread_cond_signal(car->current)) {
      fprintf(stderr, "Error signaling to unblock threads on condition variable\n");
      return -1;
    }
  }
  
  // signal other direction's waiting cars; if ledyard.num_cars
  // was NOT 0, this loop won't run and no signal will be sent here
  for (i = 0; i < num_sig_other; i++) {
    if (pthread_cond_signal(car->other)) {
      fprintf(stderr, "Error signaling to unblcok threads on condition variable\n");
      return -1;
    }
  }
  
  printf("--- A car has exited for %s ---\n", car->str_dir);

  if (pthread_mutex_unlock(&ledyard.lock)) {
    fprintf(stderr, "Error releasing lock for exit_bridge()\n");
    return -1;
  }

  return 0;
}

/* Handles one car thread's bridge-crossing. The life of the
 * car thread begins in this function. The argument is the
 * direction of the new car, and is reassigned as an int*. 
 *
 * The lifetime of the car_t* struct begins and ends with the function,
 * with variable time spent on/off bridge depending on bridge state and
 * randomness in "sleep" times when in possible interleavings.
 *
 * @param vargp a void* pointing to the direction of the new car
 * @return NULL as no return is needed when using pthread_create
 */
static void* one_vehicle(void* vargp) {
  int* dir = vargp;
  car_t* car = (car_t*) malloc(sizeof(car_t));

  // Initialize Car
  initialize_car(car, *dir);

  consider_interleaving(1, 1); // no need to make sleep long, 
                               // just used for interleaving
  // Arrive Bridge
  arrive_bridge(car);

  consider_interleaving(1, 5); // the next two "consider_interleaving"
                               // do need variable sleep time for 
  // Print Bridge              // variable times driving on the bridge
  on_bridge(car);              // before exiting

  consider_interleaving(1, 5);

  // Exit Bridge
  exit_bridge(car);

  // clean up car
  free(car->str_dir);
  car->str_dir = NULL;
  free(car);
  car = NULL;
  
  return NULL;
}

/************************ LOCAL PROGRAM FUNCTIONS *********************/

/* Initialize the begining state of the ledyard bridge
 * with no direction, 0 cars on it, and 0 cars waiting.
 * Also initializes the mutex and the condition variables
 *
 * Note: the caller will destroy the mutex and two cond variables,
 * and free ledyard.str_dir later
 *
 * @return 0 on success, -1 on error initializing pthread mutex/condvar
 */
static int initialize_bridge(void) {
  if (pthread_mutex_init(&ledyard.lock, NULL) ||
      pthread_cond_init(&ledyard.want_to_hanover, NULL) ||
      pthread_cond_init(&ledyard.want_to_norwich, NULL)) {
    fprintf(stderr, "Error initializing ledyard mutex or condition variables\n");
    return -1;
  }
  
  ledyard.str_dir = (char*) malloc((strlen("Hanover") + 1) * sizeof(char));
  strcpy(ledyard.str_dir, "Neither");
  ledyard.dir = NO_DIRECTION;
  ledyard.num_cars = 0;
  ledyard.wait_hanover = 0;
  ledyard.wait_norwich = 0;
 
  return 0;
}

/* Introduces the user to the program, returning user-inputted
 * car directions and editing total_cars ptr, if user desired. 
 * if not desired, the function signals that with a NULL return
 * 
 * The caller must free the returned int* later
 *
 * @param total_cars the int* to edit if necessary
 * @return an int* of desired car directions
 *         or NULL if randomness is desired
 */
static int* intro(int* total_cars) {
  char* buffer = (char*) malloc((strlen("100") + 1) * sizeof(char));
  
  printf("\nWelcome to the Ledyard Bridge Construction Zone!\n");
  printf("------------------------------------------------\n");
  int rc;
  int tries = 0;
  // no empty, no non-'y'/'n', no char overflow
  while (strlen(buffer) != 1 || (strcmp(buffer, "y") != 0 && strcmp(buffer, "n") != 0) || rc == -1) {
    if (tries > 0)
      printf("Answer must be 'y' or 'n'. Please try again.\n");

    rc = input_string("Would you like to control the entering cars in the simulation? (y/n): ", buffer, 2); // 2 bytes for 'y' or 'n'
    tries++;
  }
  
  // exit if no user control desired aka not "y"
  if (strcmp(buffer, "y") != 0) {
    free(buffer);
    return NULL;
  }
  
  // continue & ask for # of cars desired
  int val = -1;
  char* extra = NULL;
  while(val < 0) {
    if (val == -2)
      printf("Answer must be a number from 1-100. Please try again.\n");
    
    rc = input_string("How many cars to add to the simulation (max 100): ", buffer, 4); // 4 bytes for max 3 digits
    val = strtol(buffer, &extra, 10); // convert string to int in base 10
    if (strlen(buffer) < 1 || extra[0] != '\0' || val > 100 || val < 1 || rc == -1) // no empty, no non-#, 1 <= val <= 100, no char overflow
      val = -2; // ask user again
  }
  
  // continue & ask for each car's direction, putting into an int array
  int i;
  int str_len = strlen("Direction for car 99? (0 = Hanover, 1 = Norwich): ");
  char* message = (char*) malloc((str_len + 1) * sizeof(char));
  int* car_dirs = (int*) malloc(val * sizeof(int));
  *total_cars = val; // save total cars for function caller
  val = -1; // reuse val variable
  for (i = 0; i < *total_cars; i++) {
    while(val < 0) {
      if (val == -2)
	printf("Answer must be '0' or '1'. Please try again\n");

      sprintf(message, "Direction for car %d? (0 = Hanover, 1 = Norwich): ", i);
      rc = input_string(message, buffer, 2); // 2 bytes for '0' or '1'
      val = strtol(buffer, &extra, 10);
      if (strlen(buffer) != 1 || extra[0] != '\0' || (val != 0 && val != 1) || rc == -1) // no empty, no non-#, no non-0/1, no char overflow
	val = -2; // ask again
    }
    car_dirs[i] = val; // assign the direction for that car
    val = -1; // reset for next car assign
  }
  free(message);
  free(buffer);
  message = NULL;
  buffer = NULL;
  
  return car_dirs;
}

/* Runs a simulation of the Ledyard Bridge Construction Zone.
 * If car_dirs is NULL, the function randomly selects which 
 * direction each new car thread begins with
 *
 * Notes: (1) Randomness throughout the program is used with rand(), seeded
 * with the somewhat random "stopwatch-selected" microseconds
 * (2) total_cars must be passed in as sizeof() doesn't work on 
 * car_dirs once passed into simulation() bc it turns into int*
 *
 * @param total_cars the total cars to be added to simulation
 * @param car_dirs an int ptr to the desired directions for each car/index
 * @return early on invalid car_dirs
 */
static void simulation(int total_cars, int* car_dirs) {
  int hanover = TO_HANOVER;
  int norwich = TO_NORWICH;
  int choice;
  int* chosen_dir; // bc pthread_create requires ptr, and &TO_HANOVER doesn't work
  pthread_t car[total_cars]; // the cars threads

  // seeding random
  struct timeval t;
  gettimeofday(&t, NULL);      // UINT_MAX to ensure 32-bit system (max 2^16)
  srand(t.tv_usec % UINT_MAX); // can handle possibly 6-digits 

  // beginning simulation
  if (car_dirs == NULL)
    printf("\nDefault random simulation of 20 cars will begin...\n");
  else
    printf("\nA simulation of %d cars of specified directions will begin...\n", total_cars);
  printf("=============== SIMULATION BEGINNING ===============\n");
  int i;
  for (i = 0; i < total_cars; i++) {
    if (car_dirs != NULL) { // select user-determined directions
      if (car_dirs[i] != 0 && car_dirs[i] != 1) {
	fprintf(stderr, "Error, passed in directions are not valid\n");
	return;
      }
      choice = car_dirs[i];
    }
    else // or proceed with random car directions
      choice = rand() % 2;

    // evaluate choice as int* for pthread_create
    if (choice == 0)
      chosen_dir = &hanover;
    else if(choice == 1)
      chosen_dir = &norwich;
    // Add a new car, continue even if error
    if (pthread_create(&car[i], NULL, one_vehicle, (void*) chosen_dir))
      fprintf(stderr, "Error creating new car thread %d\n", i);

    // comment next line out if you want to frontload all cars to bridge
    consider_interleaving(1,3); 
  }

  // wait for all cars to finish executing before returning
  for (i = 0; i < total_cars; i++) {
    if (pthread_join(car[i], NULL))
      fprintf(stderr, "Error waiting for car thread %d to terminate\n", i);
  }

  printf("\nAll cars have safely exited the bridge\n");
  printf("============= SIMULATION COMPLETED ==============\n");
}

/* Manages simulation(s) based on user input */
static void manage_sims(void) {
  char* buffer = (char*) malloc(2 * sizeof(char));
  int rc, run_again = 1;
  while (run_again) {
    // ask the user if they want some control of simulation
    int total_cars = 20; // default # of cars in simulation
    int* car_dirs = intro(&total_cars); // ask user if they want control
    
    // begin the simulation
    simulation(total_cars, car_dirs);

    // free car_dirs if allocated
    if (car_dirs != NULL) {
      free(car_dirs);
      car_dirs = NULL;
    }
    rc = input_string("\nType 'y' to play again, any other key to exit: ", buffer, 2); // 2 bytes for 'y' or 'n'
    if (rc == -1 || strcmp(buffer, "y") != 0)
      run_again = 0;
  }
  free(buffer);
}

/* Destroys the bridge's mutex and two condition variables, 
 * and frees ledyard.str_dir
 * 
 * Note: even if the mutex and/or the first cond variable listed below
 * errors, the function continues trying to destroy the remaining in
 * order to salvage as much as possible (aka do its best cleaning up 
 * instead of instantly returning)
 *
 * @return 0 on success, -1 if error destroying any of the variables
 */
static int destroy_bridge(void) {
  free(ledyard.str_dir);
  ledyard.str_dir = NULL;
  
  int destroy_error = 0;
  if (pthread_mutex_destroy(&ledyard.lock)) {
    fprintf(stderr, "Error destroying ledyard mutex\n");
    destroy_error = -1;
  }
  if (pthread_cond_destroy(&ledyard.want_to_hanover)) {
    fprintf(stderr, "Error destroying ledyard condition variable\n");
    destroy_error = -1;
  }
  if (pthread_cond_destroy(&ledyard.want_to_norwich)) {
    fprintf(stderr, "Error destroying ledyard condition variable\n");
    destroy_error = -1;
  }
  
  return destroy_error;
}

/************************* MAIN ****************************/

/* Runs the ledyard program. Arguments are unused
 *
 * @return 0 on successful simulation, -1 on any error
 */
int main(void) {
  // initialize the ledyard bridge
  if (initialize_bridge())
    return -1;

  // run and manage simulations based on user input
  manage_sims();
  
  // destroy ledyard mutex and cond variables
  // and exit with its return value
  return destroy_bridge();
}
