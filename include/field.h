#ifndef FIELD_H
#define FIELD_H

#include "world.h"

// Как координатор поля общается с процессами-участниками. Реализаций две:
// через разделяемую память (src/shm_backend.c) и через каналы (apps/fifo_field.c)
typedef struct {
    void* ctx;
    // раздать команду всем работающим участникам и дождаться, пока каждый её
    // выполнит; false, если какой-то процесс-участник аварийно завершился
    bool (*run_phase)(void* ctx, World* world, Command command);
    // завершить процесс участника index и дождаться подтверждения
    void (*stop_actor)(void* ctx, World* world, int index);
} Backend;

// главный цикл: крутит такты, пока игра не закончится; по окончании
// все процессы-участники завершены
void field_run(World* world, const Backend* backend);
// завершить всех работающих участников (например, если игра не началась)
void field_stop_all(World* world, const Backend* backend);

// код завершения программы по исходу игры
int outcome_exit_code(Outcome outcome);

#endif
