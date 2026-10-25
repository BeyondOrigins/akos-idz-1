#ifndef EVENTS_H
#define EVENTS_H

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

// событие не привязано к клетке поля
#define EVENT_NO_CELL (-1)

// кто записал событие; по этому номеру события упорядочиваются при выводе,
// поэтому порядок строк не зависит от того, какой поток успел первым
#define EVENT_SOURCE_SIGNALS INT_MIN  // сообщения о сигналах идут первыми
#define EVENT_SOURCE_FORMATION 0
#define EVENT_SOURCE_CANNON 1
#define EVENT_SOURCE_FIELD INT_MAX

typedef struct {
    char text[256];  // кириллица занимает 2 байта на букву
    bool verbose;  // только с --verbose
    int x;         // клетка столкновения или EVENT_NO_CELL
    int y;
    int source;    // EVENT_SOURCE_*
    int seq;       // порядковый номер записи в такте
} Event;

// события одного такта, печатаются вместе с полем;
// пишут в журнал несколько потоков, поэтому он защищён мьютексом
typedef struct {
    Event* items;
    int count;
    int capacity;
    pthread_mutex_t lock;
} EventLog;

void events_init(EventLog* log);
void events_free(EventLog* log);
void events_clear(EventLog* log);
// событие от координатора поля; текст как в printf,
// атрибут просит компилятор проверить аргументы
void events_add(EventLog* log, bool verbose, int x, int y, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));
// событие от потока-участника source
void events_add_from(EventLog* log, int source, bool verbose, int x, int y, const char* fmt, ...)
    __attribute__((format(printf, 6, 7)));
// упорядочить по источнику, внутри источника по времени записи
void events_sort(EventLog* log);

#endif
