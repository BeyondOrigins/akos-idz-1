#ifndef SYNC_H
#define SYNC_H

#include <semaphore.h>
#include <stdbool.h>

#include "config.h"

// семафоры, которыми пользуется текущий процесс. Именованный семафор у каждого
// процесса открыт по своему адресу, неименованный лежит в разделяемой памяти;
// остальной код этой разницы не видит и работает через указатели
typedef struct {
    sem_t* go[MAX_ACTORS];  // координатор разрешает участнику выполнить команду
    sem_t* phase_done;      // участник сообщает, что команда выполнена
    sem_t* events_lock;     // двоичный семафор журнала событий; NULL — не нужен
    sem_t* stats_lock;      // двоичный семафор статистики; NULL — не нужен
} SemSet;

extern SemSet g_sems;

// жив ли тот, от кого ждём семафор
typedef bool (*AliveFn)(void* arg);

// именованный семафор: старый с тем же именем (остался от аварийного запуска)
// удаляется. Возвращают NULL при ошибке
sem_t* named_sem_create(const char* name, unsigned int value);
sem_t* named_sem_open(const char* name);  // NULL, если ещё не создан
void named_sem_close(sem_t* sem);
void named_sem_unlink(const char* name);

// неименованный семафор, разделяемый между процессами (pshared = 1)
bool unnamed_sem_init(sem_t* sem, unsigned int value);
void unnamed_sem_destroy(sem_t* sem);

// ждать, повторяя при прерывании сигналом
void sem_wait_retry(sem_t* sem);
// ждать; false, если ожидание прервал сигнал
bool sem_wait_interruptible(sem_t* sem);
// ждать, периодически проверяя, жив ли тот, кто должен сделать post;
// false, если он завершился
bool sem_wait_alive(sem_t* sem, AliveFn alive, void* arg);
void sem_post_checked(sem_t* sem);

// захват и освобождение двоичного семафора; NULL — ничего не делать
void sem_lock(sem_t* sem);
void sem_unlock(sem_t* sem);

#endif
