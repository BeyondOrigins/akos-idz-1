#ifndef EVENTS_H
#define EVENTS_H

#include <limits.h>
#include <stdbool.h>

// событие не привязано к клетке поля
#define EVENT_NO_CELL (-1)

// сколько событий помещается в журнал одного такта
#define MAX_EVENTS 512

// кто записал событие; по этому номеру события упорядочиваются при выводе,
// поэтому порядок строк не зависит от того, какой процесс успел первым
#define EVENT_SOURCE_FORMATION 0
#define EVENT_SOURCE_CANNON 1
#define EVENT_SOURCE_FIELD INT_MAX

typedef struct {
    char text[256];  // кириллица занимает 2 байта на букву
    bool verbose;    // только с --verbose
    int x;           // клетка столкновения или EVENT_NO_CELL
    int y;
    int source;      // EVENT_SOURCE_*
    int seq;         // порядковый номер записи в такте
} Event;

// события одного такта, печатаются вместе с полем. Журнал лежит в разделяемой
// памяти, в него пишут несколько процессов, поэтому он защищён семафором
// g_sems.events_lock
typedef struct {
    Event items[MAX_EVENTS];
    int count;
    int dropped;  // не поместилось
} EventLog;

void events_clear(EventLog* log);
// событие от координатора поля; текст как в printf,
// атрибут просит компилятор проверить аргументы
void events_add(EventLog* log, bool verbose, int x, int y, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));
// событие от процесса-участника source
void events_add_from(EventLog* log, int source, bool verbose, int x, int y, const char* fmt, ...)
    __attribute__((format(printf, 6, 7)));
// добавить готовое событие (пришло от участника по каналу)
void events_append(EventLog* log, const Event* event);
// упорядочить по источнику, внутри источника по времени записи
void events_sort(EventLog* log);

#endif
