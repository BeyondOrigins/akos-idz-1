#include "../include/sync.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// как часто проверять, жив ли тот, от кого ждём семафор
#define ALIVE_CHECK_NS 100000000L  // 100 мс

SemSet g_sems;

void sync_failed(const char* what) {
    fprintf(stderr, "Ошибка синхронизации: %s: %s\n", what, strerror(errno));
    exit(EXIT_FAILURE);
}

sem_t* named_sem_create(const char* name, unsigned int value) {
    sem_unlink(name);  // мог остаться после аварийного завершения
    sem_t* sem = sem_open(name, O_CREAT | O_EXCL, 0600, value);
    if (sem == SEM_FAILED) {
        fprintf(stderr, "Ошибка: не удалось создать семафор %s: %s\n", name, strerror(errno));
        return NULL;
    }
    return sem;
}

sem_t* named_sem_open(const char* name) {
    sem_t* sem = sem_open(name, 0);
    return sem == SEM_FAILED ? NULL : sem;
}

void named_sem_close(sem_t* sem) {
    if (sem != NULL) {
        sem_close(sem);
    }
}

void named_sem_unlink(const char* name) {
    sem_unlink(name);
}

bool unnamed_sem_init(sem_t* sem, unsigned int value) {
    // второй аргумент 1: семафор разделяется между процессами
    if (sem_init(sem, 1, value) != 0) {
        fprintf(stderr, "Ошибка: sem_init: %s\n", strerror(errno));
        return false;
    }
    return true;
}

void unnamed_sem_destroy(sem_t* sem) {
    sem_destroy(sem);
}

void sem_wait_retry(sem_t* sem) {
    while (sem_wait(sem) != 0) {
        if (errno != EINTR) {
            sync_failed("sem_wait");
        }
    }
}

bool sem_wait_interruptible(sem_t* sem) {
    if (sem_wait(sem) == 0) {
        return true;
    }
    if (errno != EINTR) {
        sync_failed("sem_wait");
    }
    return false;
}

#if defined(__APPLE__)
// в macOS нет sem_timedwait: пробуем захватить и спим понемногу
bool sem_wait_alive(sem_t* sem, AliveFn alive, void* arg) {
    long sleep_ns = 20000;
    long since_check = 0;
    for (;;) {
        if (sem_trywait(sem) == 0) {
            return true;
        }
        if (errno != EAGAIN && errno != EINTR) {
            sync_failed("sem_trywait");
        }
        if (since_check >= ALIVE_CHECK_NS) {
            since_check = 0;
            if (alive != NULL && !alive(arg)) {
                return false;
            }
        }
        struct timespec pause = {0, sleep_ns};
        nanosleep(&pause, NULL);
        since_check += sleep_ns;
        if (sleep_ns < 5000000L) {
            sleep_ns *= 2;
        }
    }
}
#else
// ждём порциями по 100 мс; между ними проверяем, жив ли тот, кто должен сделать post
bool sem_wait_alive(sem_t* sem, AliveFn alive, void* arg) {
    for (;;) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += ALIVE_CHECK_NS;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        if (sem_timedwait(sem, &deadline) == 0) {
            return true;
        }
        if (errno != ETIMEDOUT && errno != EINTR) {
            sync_failed("sem_timedwait");
        }
        if (alive != NULL && !alive(arg)) {
            return false;
        }
    }
}
#endif

void sem_post_checked(sem_t* sem) {
    if (sem_post(sem) != 0) {
        sync_failed("sem_post");
    }
}

void sem_lock(sem_t* sem) {
    if (sem != NULL) {
        sem_wait_retry(sem);
    }
}

void sem_unlock(sem_t* sem) {
    if (sem != NULL) {
        sem_post_checked(sem);
    }
}
