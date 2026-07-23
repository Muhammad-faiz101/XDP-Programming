#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define SIZE 10000

// Moved matrix declarations to global scope
int matrix1[SIZE][SIZE];
int matrix2[SIZE][SIZE];
int result[SIZE][SIZE];

int main(void)
{
    struct timespec start, end;

    // Initialize matrix1
    for (int r = 0; r < SIZE; r++)
    {
        for (int c = 0; c < SIZE; c++)
        {
            matrix1[r][c] = (rand() % 101) + SIZE;
        }
    }

    // Initialize matrix2
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            matrix2[i][j] = (rand() % 101) + SIZE;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Naive 3-loop matrix multiplication
    for (int x = 0; x < SIZE; x++)
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

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate total time in seconds
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Sequential %f\n", elapsed);

    return 0;
}