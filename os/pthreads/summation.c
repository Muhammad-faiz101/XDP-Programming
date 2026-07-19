#include<pthread.h>
#include<stdio.h>
#include<stdlib.h> // for actoi()

//global declaration
int sum;
void *runner(void *param); //threads call this func

int main(int argc, char *argv[]) //argc: number of strings pointed to by argv
{
    pthread_t tid ;
    pthread_attr_t attr; //set of th. attrs

    pthread_attr_init(&attr); //set default attrs

    pthread_create(&tid, &attr, runner, argv[1]);

    pthread_join(tid, NULL); //wait for th. to exit; synchr. threading

    printf("Sum = %d\n", sum);
}

void *runner(void *p)
{
    int i, upper = atoi(p);
    sum=0;

    for (i=1; i<=upper; i++){
        sum+=i;
    }

    pthread_exit(0);
}
/*
gcc summation.c -o sum
./sum 5

op: Sum = 15
*/