#include<pthread.h>
#include<stdio.h>
#define NUM_THREADS 5

void *runner(void *param); 

int main(int argc, char *argv[])
{
    int i, policy;
    pthread_t tid[NUM_THREADS];
    pthread_attr_t attr;

    //get defualt attrs
    pthread_attr_init(&attr);

    //get the current scheduling policy
    if(pthread_attr_getschedpolicy(&attr    ,   &policy) != 0) 
    {
        fprintf(stderr, "Unable to get the policy!.\n");
    }
    else
    {
        if (policy == SCHED_OTHER)
            printf("SCHED_OTHER!\n");
        else if (policy == SCHED_RR)
            printf("SCHED_RR!\n");
        else if (policy == SCHED_FIFO)
            printf("SCHED_FIFO!\n");
    }

    //set the sched policy - fifo, rr, other
    if (pthread_attr_setschedpolicy(&attr,  SCHED_FIFO) != 0)
    fprintf(stderr, "Unable to set the policy!\n");
    else
    printf("SCHED_FIFO set.\n");

    //create threads
    for (i=0; i<NUM_THREADS; i++)
        pthread_create(&tid[i], &attr,  runner, NULL);

    //join on each thrd
    for (i=0; i<NUM_THREADS; i++)
        pthread_join(tid[i], NULL);
}


//each to begin control in this function
void *runner(void *param){
    printf("hi there!\n");
    pthread_exit(0);
}

/*
to compile:
gcc posix-rlts-api.c -o sched -lpthread

to run: sudo ./sched
*/