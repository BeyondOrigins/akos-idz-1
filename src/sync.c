#include "../include/sync.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ThreadSanitizer на macOS не перехватывает sem_wait/sem_post и не видит,
// что семафор упорядочивает обращения к памяти (POSIX это гарантирует).
// В сборке с санитайзером сообщаем ему об этом явно, в обычной сборке ничего нет
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define IDZ_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define IDZ_TSAN 1
#endif

#ifdef IDZ_TSAN
void __tsan_acquire(void* addr);
void __tsan_release(void* addr);
#define TSAN_ACQUIRE(addr) __tsan_acquire(addr)
#define TSAN_RELEASE(addr) __tsan_release(addr)
#else
#define TSAN_ACQUIRE(addr) ((void)(addr))
#define TSAN_RELEASE(addr) ((void)(addr))
#endif

void sync_failed(const char* what, int err) {
    fprintf(stderr, "Ошибка синхронизации: %s: %s\n", what, strerror(err));
    abort();
}

sem_t* semaphore_create(unsigned int value) {
#ifdef __APPLE__
    // семафоры создаются только из главного потока, счётчик без защиты
    static int counter = 0;
    char name[32];
    for (;;) {
        snprintf(name, sizeof(name), "/idz2-%d-%d", (int)getpid(), counter++);
        sem_t* sem = sem_open(name, O_CREAT | O_EXCL, 0600, value);
        if (sem == SEM_FAILED) {
            if (errno == EEXIST) {
                continue;  // имя осталось от чужого процесса, берём следующее
            }
            return NULL;
        }
        // имя больше не нужно: семафор живёт, пока открыт
        sem_unlink(name);
        return sem;
    }
#else
    sem_t* sem = malloc(sizeof(sem_t));
    if (sem == NULL) {
        return NULL;
    }
    if (sem_init(sem, 0, value) != 0) {
        free(sem);
        return NULL;
    }
    return sem;
#endif
}

void semaphore_destroy(sem_t* sem) {
    if (sem == NULL) {
        return;
    }
#ifdef __APPLE__
    sem_close(sem);
#else
    sem_destroy(sem);
    free(sem);
#endif
}

void semaphore_wait(sem_t* sem) {
    while (sem_wait(sem) != 0) {
        if (errno != EINTR) {
            sync_failed("sem_wait", errno);
        }
    }
    TSAN_ACQUIRE(sem);
}

void semaphore_post(sem_t* sem) {
    TSAN_RELEASE(sem);
    if (sem_post(sem) != 0) {
        sync_failed("sem_post", errno);
    }
}

void mutex_init(pthread_mutex_t* mutex) {
    int err = pthread_mutex_init(mutex, NULL);
    if (err != 0) {
        sync_failed("pthread_mutex_init", err);
    }
}

void mutex_destroy(pthread_mutex_t* mutex) {
    int err = pthread_mutex_destroy(mutex);
    if (err != 0) {
        sync_failed("pthread_mutex_destroy", err);
    }
}

void mutex_lock(pthread_mutex_t* mutex) {
    int err = pthread_mutex_lock(mutex);
    if (err != 0) {
        sync_failed("pthread_mutex_lock", err);
    }
}

void mutex_unlock(pthread_mutex_t* mutex) {
    int err = pthread_mutex_unlock(mutex);
    if (err != 0) {
        sync_failed("pthread_mutex_unlock", err);
    }
}
