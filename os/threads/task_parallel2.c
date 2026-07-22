#define _POSIX_C_SOURCE 199309L // Unlocks high-precision CLOCK_MONOTONIC
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

// Task A: Computes a large factorial (Iterative Math Operation)
void* factorial_task(void* arg) {
    long long* result = malloc(sizeof(long long));
    long long fact = 1;
    for (int i = 1; i <= 20; i++) {
        fact *= i;
    }
    *result = fact;
    return (void*)result;
}

// Task B: Computes a Fibonacci number (Sequence Math Operation)
void* fibonacci_task(void* arg) {
    long long* result = malloc(sizeof(long long));
    long long a = 0, b = 1, next;
    for (int i = 2; i <= 50; i++) {
        next = a + b;
        a = b;
        b = next;
    }
    *result = b;
    return (void*)result;
}

// Task C: Computes the sum of a matrix diagonal (Array Math Operation)
void* matrix_diagonal_task(void* arg) {
    long long* result = malloc(sizeof(long long));
    long long sum = 0;
    // Simulate processing a 500x500 matrix diagonal
    for (int i = 0; i < 500; i++) {
        sum += (i * 2); 
    }
    *result = sum;
    return (void*)result;
}

int main() {
    pthread_t thread1, thread2, thread3;
    void *ptr_fact, *ptr_fib, *ptr_mat;
    struct timespec start, end;

    // Start high-precision wall-clock timer
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("Spawning 3 completely distinct math tasks simultaneously...\n\n");

    // TASK PARALLELISM: Launching different functions at the exact same time
    pthread_create(&thread1, NULL, factorial_task, NULL);
    pthread_create(&thread2, NULL, fibonacci_task, NULL);
    pthread_create(&thread3, NULL, matrix_diagonal_task, NULL);

    // Wait for each distinct task to finish and grab their returned values
    pthread_join(thread1, &ptr_fact);
    pthread_join(thread2, &ptr_fib);
    pthread_join(thread3, &ptr_mat);

    // Stop high-precision wall-clock timer
    clock_gettime(CLOCK_MONOTONIC, &end);

    // Cast the generic returned pointers back into usable data
    long long final_fact = *(long long*)ptr_fact;
    long long final_fib  = *(long long*)ptr_fib;
    long long final_mat  = *(long long*)ptr_mat;

    // Free allocated heap memory to prevent leaks
    free(ptr_fact);
    free(ptr_fib);
    free(ptr_mat);

    // Print calculated results
    printf("Result of Task A (Factorial 20): %lld\n", final_fact);
    printf("Result of Task B (Fibonacci 50): %lld\n", final_fib);
    printf("Result of Task C (Matrix Diagonal Sum): %lld\n", final_mat);

    // Calculate stopwatch elapsed wall-clock time
    double time_taken = (end.tv_sec - start.tv_sec) + 
                        (end.tv_nsec - start.tv_nsec) / 1000000000.0;
                        
    printf("\nTotal Wall-Clock Time Elapsed: %.6f seconds\n", time_taken);

    return 0;
}
