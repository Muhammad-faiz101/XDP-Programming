#define _POSIX_C_SOURCE 199309L // Crucial for unlocking CLOCK_MONOTONIC on Linux/macOS
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define ROWS1 500
#define COLS1 500
#define ROWS2 300
#define COLS2 500
#define NUM_THREADS 4

int mat1[ROWS1][COLS1];
int mat2[ROWS2][COLS2];
int result[ROWS1][COLS2];

typedef struct {
    int start_row;
    int end_row;
} ThreadData;

void* multiply_worker(void* arg) {
    ThreadData* data = (ThreadData*)arg;

    for (int i = data->start_row; i < data->end_row; i++) {
        for (int j = 0; j < COLS2; j++) {
            int total = 0;
            for (int k = 0; k < ROWS2; k++) {
                total += mat1[i][k] * mat2[k][j];
            }
            result[i][j] = total;
        }
    }
    return NULL;
}

int main() {
    srand((unsigned int)time(NULL));

    // Populate matrices
    for (int i = 0; i < ROWS1; i++) {
        for (int j = 0; j < COLS1; j++) {
            mat1[i][j] = (rand() % 10) + 1;
        }
    }

    for (int i = 0; i < ROWS2; i++) {
        for (int j = 0; j < COLS2; j++) {
            mat2[i][j] = (rand() % 10) + 1;
        }
    }

    // HIGH PRECISION TIMING START
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    int rows_per_thread = ROWS1 / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].start_row = i * rows_per_thread;
        thread_data[i].end_row = (i == NUM_THREADS - 1) ? ROWS1 : (i + 1) * rows_per_thread;
        pthread_create(&threads[i], NULL, multiply_worker, &thread_data[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // HIGH PRECISION TIMING END
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate precise fractional time difference
    double time_taken = (end.tv_sec - start.tv_sec) + 
                        (end.tv_nsec - start.tv_nsec) / 1000000000.0;

    printf("Total time for multithreaded execution: %f seconds\n", time_taken);
    printf("%.6f seconds\n", time_taken);

    return 0;
}
