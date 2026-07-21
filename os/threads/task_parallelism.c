#include <stdio.h>
#include <pthread.h>

int result_a = 0;
int result_b = 0;

// task a
void* add_task(void* arg) {
    result_a = 2 + 3;
    return NULL;
}

// task b
void* subtract_task(void* arg) {
    result_b = 10 - 4;
    return NULL;
}

int main() {
    pthread_t thread1;
    pthread_t thread2;
    int final_answer;

    // calculations in parallel
    pthread_create(&thread1, NULL, add_task, NULL);
    pthread_create(&thread2, NULL, subtract_task, NULL);

    // wait both independent threads to finish working
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);


    final_answer = result_a * result_b;

    printf("Final Combined Result: %d\n", final_answer); // Output: 30

    return 0;
}
