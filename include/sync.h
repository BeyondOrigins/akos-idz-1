#ifndef SYNC_H
#define SYNC_H

#include <pthread.h>
#include <semaphore.h>

// семафор: на Linux неименованный (sem_init); на macOS sem_init не работает,
// там именованный (sem_open), имя сразу удаляется через sem_unlink.
// возвращает NULL, если семафор создать не удалось
sem_t* semaphore_create(unsigned int value);
void semaphore_destroy(sem_t* sem);
// sem_wait, который повторяется, если ожидание прервал сигнал
void semaphore_wait(sem_t* sem);
void semaphore_post(sem_t* sem);

// операции с мьютексом; при ошибке программа аварийно завершается
void mutex_init(pthread_mutex_t* mutex);
void mutex_destroy(pthread_mutex_t* mutex);
void mutex_lock(pthread_mutex_t* mutex);
void mutex_unlock(pthread_mutex_t* mutex);

#endif
