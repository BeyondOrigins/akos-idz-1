#ifndef SHM_BACKEND_H
#define SHM_BACKEND_H

#include "field.h"
#include "sync.h"

// Взаимодействие через разделяемую память: мир общий, координатор пишет
// команду в таблицу участников и поднимает семафор go[i], участник выполняет
// её над общим миром и поднимает phase_done. Используется программами с fork
// (оба вида семафоров) и независимыми программами с разделяемой памятью.
typedef struct {
    bool children;               // участники — дочерние процессы (fork)
    bool reaped[MAX_ACTORS];     // дочерний процесс уже подобран waitpid
} ShmBackend;

Backend shm_backend(ShmBackend* backend, bool children);

// цикл процесса-участника: ждёт команду на g_sems.go[index], выполняет,
// отчитывается в g_sems.phase_done. Пока ждёт, проверяет, жив ли координатор.
// Возвращает false, если координатор пропал, не прислав команду STOP
bool shm_participant_loop(World* world, int index, AliveFn coordinator_alive, void* arg);

// жив ли процесс с данным pid (для независимо запущенных программ)
bool pid_alive(void* pid_ptr);

#endif
