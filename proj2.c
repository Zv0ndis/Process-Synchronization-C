/*
Author: Tomas Zvonicek
Login: xzvonit00

mental breakdowns: lost the count
*/

#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum Semaphores {
    SEM_MUTEX,
    SEM_BOARDING_MUTEX,
    SEM_PRINT,
    SEM_PERSONAL_DOCK_0,
    SEM_PERSONAL_DOCK_1,
    SEM_CARGO_DOCK_0,
    SEM_CARGO_DOCK_1,
    SEM_BOARDING,
    SEM_ALL_ABOARD,
    SEM_UNLOAD,
    SEM_COUNT
};

// VEhicle types and their weights
enum VehicleType { PERSONAL = 1, CARGO = 3 };

typedef struct {
    int action_id;
    int personal_waiting[2];
    int cargo_waiting[2];
    int current_weight;
    int boarding_complete;
    int vehicles_on_ferry;
    int dock_status[2];
    int max_weight;
    FILE *output_file;
} SharedData;

sem_t *semaphores;
SharedData *shared;

void print_action(const char *fmt, ...) { // File write
    sem_wait(&semaphores[SEM_PRINT]);
    fprintf(shared->output_file, "%d: ", shared->action_id++);
    va_list args;
    va_start(args, fmt);
    vfprintf(shared->output_file, fmt, args);
    va_end(args);
    fflush(shared->output_file);
    sem_post(&semaphores[SEM_PRINT]);
}

void error_exit(const char *msg) { // Error handling
    perror(msg);
    exit(EXIT_FAILURE);
}

void personal_process(int id, int TA) { // Personal vehicle process
    srand(time(NULL) + getpid());
    int dock = rand() % 2;
    print_action("O %d: started\n", id);
    usleep(rand() % (TA + 1)); // Simulates time spent by vehicle reaching the dock
    print_action("O %d: arrived to %d\n", id, dock);

    sem_wait(&semaphores[SEM_MUTEX]);
    shared->personal_waiting[dock]++;
    sem_post(&semaphores[SEM_MUTEX]);

    // wait for permission to board
    sem_wait(
        &semaphores[dock == 0 ? SEM_PERSONAL_DOCK_0 : SEM_PERSONAL_DOCK_1]);

    // boarding signal
    sem_wait(&semaphores[SEM_BOARDING]);

    sem_wait(&semaphores[SEM_MUTEX]);
    shared->personal_waiting[dock]--;
    shared->vehicles_on_ferry++;
    shared->current_weight += PERSONAL; // Add weight to ferry
    print_action("O %d: boarding\n", id);
    shared->boarding_complete++;
    sem_post(&semaphores[SEM_MUTEX]);

    sem_post(&semaphores[SEM_ALL_ABOARD]); // Signal that boarding is complete
    sem_wait(&semaphores[SEM_UNLOAD]);     // Wait for unloading signal

    int arrival_dock = (dock + 1) % 2;
    print_action("O %d: leaving in %d\n", id, arrival_dock);
    exit(EXIT_SUCCESS);
}

void cargo_process(int id, int TA) {
    srand(time(NULL) + getpid());
    int dock = rand() % 2;
    print_action("N %d: started\n", id);
    usleep(rand() % (TA + 1)); // Simulates time spent by vehicle reaching the dock
    print_action("N %d: arrived to %d\n", id, dock);

    sem_wait(&semaphores[SEM_MUTEX]);
    shared->cargo_waiting[dock]++;
    sem_post(&semaphores[SEM_MUTEX]);

    // Wait for permission to board
    sem_wait(&semaphores[dock == 0 ? SEM_CARGO_DOCK_0 : SEM_CARGO_DOCK_1]);

    // Wait for boarding signal
    sem_wait(&semaphores[SEM_BOARDING]);

    sem_wait(&semaphores[SEM_MUTEX]);
    shared->cargo_waiting[dock]--;
    shared->vehicles_on_ferry++;
    shared->current_weight += CARGO; // Add weight to ferry
    print_action("N %d: boarding\n", id);
    shared->boarding_complete++;
    sem_post(&semaphores[SEM_MUTEX]);

    sem_post(&semaphores[SEM_ALL_ABOARD]);
    sem_wait(&semaphores[SEM_UNLOAD]);

    int arrival_dock = (dock + 1) % 2;
    print_action("N %d: leaving in %d\n", id, arrival_dock);
    exit(EXIT_SUCCESS);
}

void ferry_process(int TP) {
    print_action("P: started\n");
    usleep(rand() % (TP + 1)); // Simulates time spent by ferry reaching the dock
    int current_dock = 0;
    print_action("P: arrived to %d\n", current_dock);

    while (true) { // Loop until all vehicles are transported
        usleep(500);

        sem_wait(&semaphores[SEM_MUTEX]);
        shared->boarding_complete = 0;
        shared->current_weight = 0; // Reset weight at the start of boarding
        shared->dock_status[current_dock] = 1; // Dock is now busy
        sem_post(&semaphores[SEM_MUTEX]);

        sem_wait(&semaphores[SEM_BOARDING_MUTEX]);
        int vehicles_to_board = 0;
        bool load_cargo_next =
            true; // Start with cargo vehicle (will alternate)

        // Load vehicles as long as there is capacity available
        // switch between personal and cargo vehicles
        while (true) {
            sem_wait(&semaphores[SEM_MUTEX]);
            bool loaded = false;

            // First try the preferred vehicle based on the paterin ( N / O / N
            // ...)
            if (load_cargo_next) {
                // Try cargo
                if (shared->cargo_waiting[current_dock] > 0 &&
                    shared->current_weight + CARGO <= shared->max_weight) {
                    sem_post(&semaphores[SEM_MUTEX]);
                    sem_post(&semaphores[current_dock == 0 ? SEM_CARGO_DOCK_0
                                                           : SEM_CARGO_DOCK_1]);
                    loaded = true;
                    vehicles_to_board++;
                    load_cargo_next = false; // Switch to personal next
                }
                // Try Personal
                else if (shared->personal_waiting[current_dock] > 0 &&
                         shared->current_weight + PERSONAL <=
                             shared->max_weight) {
                    sem_post(&semaphores[SEM_MUTEX]);
                    sem_post(
                        &semaphores[current_dock == 0 ? SEM_PERSONAL_DOCK_0
                                                      : SEM_PERSONAL_DOCK_1]);
                    loaded = true;
                    vehicles_to_board++;
                    load_cargo_next = true; // try cargo next time
                } else {
                    sem_post(&semaphores[SEM_MUTEX]);
                    break; // No more vehicles can be loaded
                }
            } else {
                // Try to load personal vehicle (O)
                if (shared->personal_waiting[current_dock] > 0 &&
                    shared->current_weight + PERSONAL <= shared->max_weight) {
                    sem_post(&semaphores[SEM_MUTEX]);
                    sem_post(
                        &semaphores[current_dock == 0 ? SEM_PERSONAL_DOCK_0
                                                      : SEM_PERSONAL_DOCK_1]);
                    loaded = true;
                    vehicles_to_board++;
                    load_cargo_next = true; // Switch to cargo next
                } // try cargo vehicle
                else if (shared->cargo_waiting[current_dock] > 0 &&
                         shared->current_weight + CARGO <= shared->max_weight) {
                    sem_post(&semaphores[SEM_MUTEX]);
                    sem_post(&semaphores[current_dock == 0 ? SEM_CARGO_DOCK_0
                                                           : SEM_CARGO_DOCK_1]);
                    loaded = true;
                    vehicles_to_board++;
                    load_cargo_next = false; // Try personal next time
                } else {
                    sem_post(&semaphores[SEM_MUTEX]);
                    break; // No more vehicles can be loaded
                }
            }

            if (loaded) {
                sem_post(&semaphores[SEM_BOARDING]);
                sem_wait(&semaphores[SEM_ALL_ABOARD]);
            }
        }

        sem_post(&semaphores[SEM_BOARDING_MUTEX]);

        sem_wait(&semaphores[SEM_MUTEX]);
        shared->dock_status[current_dock] = 0; // Dock is empty
        sem_post(&semaphores[SEM_MUTEX]);

        print_action("P: leaving %d\n", current_dock);
        usleep(rand() % (TP + 1));      // Simuate ferry travel time
        current_dock = (current_dock + 1) % 2; // Arrived to the other dock
        print_action("P: arrived to %d\n", current_dock);

        // Unload vehicles
        for (int i = 0; i < vehicles_to_board; i++) {
            sem_post(&semaphores[SEM_UNLOAD]);
            usleep(100); //
        }

        sem_wait(&semaphores[SEM_MUTEX]);
        shared->vehicles_on_ferry = 0; // Reset vehicle count
        shared->current_weight = 0;    // Reset weight
        sem_post(&semaphores[SEM_MUTEX]);

        // exit condition: if no vehicles are waiting at both docks
        if (shared->personal_waiting[0] == 0 &&
            shared->personal_waiting[1] == 0 && shared->cargo_waiting[0] == 0 &&
            shared->cargo_waiting[1] == 0) {
            break;
        }
    }

    print_action("P: leaving %d\n", current_dock); // Final departure
    sem_wait(&semaphores[SEM_BOARDING_MUTEX]);
    print_action("P: finish\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s N O K TA TP\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int N = atoi(argv[1]);  // number of cargo vehicles
    int O = atoi(argv[2]);  // Number of personal vehicles
    int K = atoi(argv[3]);  // Ferry capacity
    int TA = atoi(argv[4]); // Maximum time car travel
    int TP = atoi(argv[5]); // Maximum ferry travel time

    if (N < 0 || N >= 10000 || O < 0 || O >= 10000 || K < 3 || K > 100 ||
        TA < 0 || TA > 10000 || TP < 0 || TP > 1000) {
        fprintf(stderr, "Invalid arguments\n");
        exit(EXIT_FAILURE);
    }

    shared = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED)
        error_exit("mmap failed");

    semaphores = mmap(NULL, sizeof(sem_t) * SEM_COUNT, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (semaphores == MAP_FAILED)
        error_exit("mmap semaphores failed");

    // Initialize all semaphores
    if (sem_init(&semaphores[SEM_MUTEX], 1, 1) ||
        sem_init(&semaphores[SEM_BOARDING_MUTEX], 1, 1) ||
        sem_init(&semaphores[SEM_PRINT], 1, 1) ||
        sem_init(&semaphores[SEM_PERSONAL_DOCK_0], 1, 0) ||
        sem_init(&semaphores[SEM_PERSONAL_DOCK_1], 1, 0) ||
        sem_init(&semaphores[SEM_CARGO_DOCK_0], 1, 0) ||
        sem_init(&semaphores[SEM_CARGO_DOCK_1], 1, 0) ||
        sem_init(&semaphores[SEM_BOARDING], 1, 0) ||
        sem_init(&semaphores[SEM_ALL_ABOARD], 1, 0) ||
        sem_init(&semaphores[SEM_UNLOAD], 1, 0)) {
        error_exit("sem_init failed");
    }

    memset(shared, 0, sizeof(SharedData));
    shared->action_id = 1;
    shared->max_weight = K; // Set maximum ferry capacity
    shared->output_file = fopen("proj2.out", "w");
    if (shared->output_file == NULL) {
        error_exit("Failed to open output file");
    }
    setbuf(shared->output_file, NULL);

    pid_t ferry_pid = fork();
    if (ferry_pid == 0) {
        ferry_process(TP); // Ferry with dynamic number of trips
    } else if (ferry_pid < 0) {
        error_exit("Fork failed for ferry");
    }

    for (int i = 1; i <= O; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            personal_process(i, TA); // Personal vehicles
        } else if (pid < 0) {
            error_exit("Fork failed for personal vehicle");
        }
    }

    for (int i = 1; i <= N; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            cargo_process(i, TA); // Cargo vehicles
        } else if (pid < 0) {
            error_exit("Fork failed for cargo vehicle");
        }
    }

    for (int i = 0; i < N + O + 1; i++) { // Wait for all processes to finish
        wait(NULL);
    }

    fclose(shared->output_file);        // Close output file
    munmap(shared, sizeof(SharedData)); // Unmap shared memory

    // Close all semaphores
    for (int i = 0; i < SEM_COUNT; i++) {
        sem_destroy(&semaphores[i]);
    }
    munmap(semaphores, sizeof(sem_t) * SEM_COUNT); // Unmap semaphores

    return 0;
}