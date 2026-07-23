#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

#define SIZE 1000
#define NUM_THREADS 12

int matrix1[SIZE][SIZE];
int matrix2[SIZE][SIZE];
int result[SIZE][SIZE];

/* Information given to each thread */
typedef struct
{
    int start_row;
    int end_row;
} ThreadData;

/* Function executed by each thread */
void *multiply(void *arg)
{
    ThreadData *data = (ThreadData *)arg;

    for (int x = data->start_row; x < data->end_row; x++)
    {

        for (int y = 0; y < SIZE; y++)
        {
            result[x][y] = 0;

            for (int z = 0; z < SIZE; z++)
            {
                result[x][y] += matrix1[x][z] * matrix2[z][y];
            }
        }
    }

    return NULL;
}

int main()
{
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    struct timespec start, end;

    /* Generate matrix1 */
    for (int r = 0; r < SIZE; r++)
    {
        for (int c = 0; c < SIZE; c++)
        {
            matrix1[r][c] = (rand() % 101) + 100;
        }
    }

    /* Generate matrix2 */
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            matrix2[i][j] = (rand() % 101) + 100;
        }
    }

    /* Start measuring time */
    //clock_t start = clock();
    // time_t start =time(NULL);

    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Create threads */
    for (int i = 0; i < NUM_THREADS; i++)
    {
        thread_data[i].start_row =
            i * (SIZE / NUM_THREADS);

        thread_data[i].end_row =
            (i + 1) * (SIZE / NUM_THREADS);

        pthread_create(
            &threads[i],
            NULL,
            multiply,
            &thread_data[i]
        );
    }

    /* Wait for all threads */
    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
  /*  
    time_t end = time(NULL);

    double total_time = difftime(end,start);
     double execution_time =
        (double)(end - start) / CLOCKS_PER_SEC;
        */
    double elapsed =
    (end.tv_sec - start.tv_sec) +
    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Multithreading: %f seconds\n",
           elapsed);

    return 0;
}