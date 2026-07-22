#define _POSIX_C_SOURCE 199309L // <--- ADD THIS LINE FIRST
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


#define ROWS1 500
#define COLS1 300
#define ROWS2 300
#define COLS2 1300

int main() {
    
    srand(time(NULL));

    // Allocate matrices
    int mat1[ROWS1][COLS1];
    int mat2[ROWS2][COLS2];
    int result[ROWS1][COLS2];

    // Populate mat1 with random numbers between 1 and 10
    for (int i = 0; i < ROWS1; i++) {
        for (int j = 0; j < COLS1; j++) {
            mat1[i][j] = (rand() % 10) + 1;
        }
    }

    // Populate mat2 with random numbers between 1 and 10
    for (int i = 0; i < ROWS2; i++) {
        for (int j = 0; j < COLS2; j++) {
            mat2[i][j] = (rand() % 10) + 1;
        }
    }

    // High precision time tracking structures
    struct timespec start, end;

    // Start timing the execution
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Sequential matrix multiplication
    for (int i = 0; i < ROWS1; i++) {
        for (int j = 0; j < COLS2; j++) {
            int total = 0;
            for (int k = 0; k < ROWS2; k++) {
                total += mat1[i][k] * mat2[k][j];
            }
            result[i][j] = total;
        }
    }

    // End timing
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate final time difference in fractions of a second
    double time_taken = (end.tv_sec - start.tv_sec) + 
                        (end.tv_nsec - start.tv_nsec) / 1000000000.0;

    // Print timing results
    printf("Total time for sequential execution: %.5f seconds\n", time_taken);
    printf("Raw: %.6f seconds\n", time_taken);

    return 0;
}
