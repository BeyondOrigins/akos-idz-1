#ifndef FIFO_PROTO_H
#define FIFO_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "world.h"

// Сообщения программ si-fifo-*. Общей памяти нет: у координатора свой мир,
// у каждого участника своя копия, которую координатор обновляет снимком.
//
// Канал FIFO_JOIN (участники -> координатор): JoinRequest.
// Канал <pid>.cmd (координатор -> участник): Welcome, затем команды
//   CommandHeader + данные: PLAN — снимок мира, MOVE — план строя, STOP — ничего.
// Канал <pid>.reply (участник -> координатор): Reply после каждой команды.

typedef struct {
    pid_t pid;
    ActorKind kind;
} JoinRequest;

typedef struct {
    bool accepted;     // false — свободных мест для этой роли нет
    int index;         // место в таблице участников
    pid_t coordinator;
    Config cfg;        // по нему участник строит свою копию мира
} Welcome;

typedef struct {
    Command command;
    int tick;
} CommandHeader;

// снимок мира для фазы планирования; за заголовком идут
// aliens[alien_count], shots[shot_count] и shields[width * height]
typedef struct {
    Formation formation;
    Cannon cannon;
    int aliens_alive;
    int shot_count;
} SnapshotHeader;

#define REPLY_MAX_EVENTS 4

// ответ участника: его собственное состояние после команды, прибавка к
// статистике и события, которые он записал
typedef struct {
    int index;
    Stats stats;
    union {
        Formation formation;
        Cannon cannon;
        Alien alien;
    } state;
    int event_count;
    Event events[REPLY_MAX_EVENTS];
} Reply;

// записать или прочитать ровно size байт; повторяется при прерывании сигналом.
// read_exact возвращает false, если другой конец канала закрыт
bool write_all(int fd, const void* buf, size_t size);
bool read_exact(int fd, void* buf, size_t size);

#endif
