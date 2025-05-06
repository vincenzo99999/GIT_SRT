#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MIN_SEMIPERIOD 100000 // 100ms
#define NSEC_PER_SEC 1000000000ULL
#define NGENERATORS 3

/************************** DATA STRUCTURES *******************************/

struct seq_str {
    unsigned int bit[3];
    pthread_mutex_t lock;
};
static struct seq_str seq_data;

struct rec_str {
    unsigned int OK;
    unsigned int count;
    pthread_mutex_t lock;
};
static struct rec_str shared_rec_data;

struct periodic_thread {
    int index;
    struct timespec r;
    int period;
    int phase;
    int wcet;
    int priority;
};

/***************************** UTILITY FUNCTIONS ***********************************/

static inline void timespec_add_us(struct timespec *t, uint64_t d)
{
    d *= 1000;
    t->tv_nsec += d;
    t->tv_sec += t->tv_nsec / NSEC_PER_SEC;
    t->tv_nsec %= NSEC_PER_SEC;
}

void wait_next_activation(struct periodic_thread *thd)
{
    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &(thd->r), NULL);
    timespec_add_us(&(thd->r), thd->period);
}

void start_periodic_timer(struct periodic_thread *thd, uint64_t offs)
{
    clock_gettime(CLOCK_REALTIME, &(thd->r));
    timespec_add_us(&(thd->r), offs);
}

/***************************** THREADS ************************************/

void* rt_generator_thread(void* parameter) {
    struct periodic_thread *th = (struct periodic_thread *) parameter;
    printf("Start Generator %d\n", th->index);
    seq_data.bit[th->index] = 1;
    start_periodic_timer(th, th->phase);

    while (1) {
        wait_next_activation(th);
        pthread_mutex_lock(&seq_data.lock);
        seq_data.bit[th->index] = !seq_data.bit[th->index];
        pthread_mutex_unlock(&seq_data.lock);
    }
}

void* rt_recognizer_thread(void* parameter) {
    struct periodic_thread *th = (struct periodic_thread *) parameter;
    unsigned short int state = 0;
    unsigned short int found = 0;

    start_periodic_timer(th, th->phase);

    while (1) {
        wait_next_activation(th);
        pthread_mutex_lock(&seq_data.lock);
        int number = seq_data.bit[0] + seq_data.bit[1]*2 + seq_data.bit[2]*4;
        pthread_mutex_unlock(&seq_data.lock);

        switch (state) {
            case 0: state = (number == 0) ? 1 : 0; break;
            case 1: state = (number == 3) ? 2 : 0; break;
            case 2: state = (number == 6) ? 3 : 0; break;
            case 3:
                if (number == 5) {
                    found = 1;
                    state = 0;
                } else {
                    state = 0;
                }
                break;
            default: state = 0; break;
        }

        pthread_mutex_lock(&shared_rec_data.lock);
        if (found) {
            shared_rec_data.count++;
        }
        shared_rec_data.OK = found;
        found = 0;
        pthread_mutex_unlock(&shared_rec_data.lock);
        printf("numero attuale: %d",number);
    }
}

void* nrt_buddy_thread(void* parameter) {
    struct periodic_thread *th = (struct periodic_thread *) parameter;
    printf("Start Buddy %d\n", th->index);
    start_periodic_timer(th, th->phase);

    while (1) {
        wait_next_activation(th);
        pthread_mutex_lock(&seq_data.lock);
        printf("bits : ");
        for (int i = NGENERATORS - 1; i >= 0; i--) {
            printf("%d ", seq_data.bit[i]);
        }
        pthread_mutex_unlock(&seq_data.lock);

        pthread_mutex_lock(&shared_rec_data.lock);
        printf("\tcount: %d\t", shared_rec_data.count);
        if (shared_rec_data.OK) printf("OK");
        printf("\n");
        pthread_mutex_unlock(&shared_rec_data.lock);
    }
}

/***************************** MAIN **************************************/

int main() {
    int arraySemiperiods[NGENERATORS] = {1, 2, 4};
    int arrayPhases[NGENERATORS] = {3, 2, 1};
    int i;

    struct periodic_thread th_g[NGENERATORS];
    struct periodic_thread th_r;
    struct periodic_thread th_buddy;

    pthread_t thread_generator[NGENERATORS];
    pthread_t thread_recognizer;
    pthread_t thread_buddy;

    pthread_attr_t myattr;
    struct sched_param myparam;
    pthread_mutexattr_t mymutexattr;

    pthread_mutexattr_init(&mymutexattr);
    pthread_mutexattr_setprotocol(&mymutexattr, PTHREAD_PRIO_INHERIT);

    pthread_mutex_init(&seq_data.lock, &mymutexattr);
    pthread_mutex_init(&shared_rec_data.lock, &mymutexattr);

    pthread_attr_init(&myattr);
    pthread_attr_setschedpolicy(&myattr, SCHED_FIFO);
    pthread_attr_setinheritsched(&myattr, PTHREAD_EXPLICIT_SCHED);

    for (i = 0; i < NGENERATORS; i++) {
        th_g[i].index = i;
        th_g[i].period = arraySemiperiods[i] * MIN_SEMIPERIOD;
        th_g[i].phase = arrayPhases[i] * MIN_SEMIPERIOD;
        th_g[i].priority = 10 - i;
        myparam.sched_priority = th_g[i].priority;
        pthread_attr_setschedparam(&myattr, &myparam);
        pthread_create(&thread_generator[i], &myattr, rt_generator_thread, &th_g[i]);
    }

    th_r.index = NGENERATORS;
    th_r.period = MIN_SEMIPERIOD;
    th_r.phase = MIN_SEMIPERIOD / 2;
    th_r.priority = 20;
    myparam.sched_priority = th_r.priority;
    pthread_attr_setschedparam(&myattr, &myparam);
    pthread_create(&thread_recognizer, &myattr, rt_recognizer_thread, &th_r);

    th_buddy.index = NGENERATORS + 1;
    th_buddy.period = MIN_SEMIPERIOD;
    th_buddy.phase = MIN_SEMIPERIOD;
    pthread_create(&thread_buddy, NULL, nrt_buddy_thread, &th_buddy);

    pthread_attr_destroy(&myattr);
    pthread_mutexattr_destroy(&mymutexattr);

    while (1) {
        if (getchar() == 'q') break;
    }

    for (i = 0; i < NGENERATORS; i++) {
        pthread_cancel(thread_generator[i]);
    }
    pthread_cancel(thread_recognizer);
    pthread_cancel(thread_buddy);

    printf("EXIT!\n");
    return 0;
}
