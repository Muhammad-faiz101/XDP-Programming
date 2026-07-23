#include<stdio.h>
#include <omp.h>

int main()
{
    //seq code
    #pragma omp parallel 
    {
        printf("I am parallel region."); 
    }
    //seq code
    return 0;
}
/*
to compile: gcc -fopenmp omp-book.c -o omp
to run: ./omp

output: When OpenMP encounters the directive
#pragma omp parallel
it creates as many threads as there are processing cores in the system

It gave me statement 12 times.
*/