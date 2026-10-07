#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>

// событие не привязано к клетке поля
#define EVENT_NO_CELL (-1)

typedef struct {
    char text[192];
    bool verbose;  // только с --verbose
    int x;  // клетка столкновения или EVENT_NO_CELL
    int y;
} Event;

// события одного такта, печатаются вместе с полем
typedef struct {
    Event* items;
    int count;
    int capacity;
} EventLog;

void events_init(EventLog* log);
void events_free(EventLog* log);
void events_clear(EventLog* log);
// текст как в printf; атрибут просит компилятор проверить аргументы
void events_add(EventLog* log, bool verbose, int x, int y, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));

#endif
