#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>

#define EVENT_NO_CELL (-1)

typedef struct {
    char text[192];
    bool verbose;
    int x;
    int y;
} Event;

typedef struct {
    Event* items;
    int count;
    int capacity;
} EventLog;

void events_init(EventLog* log);
void events_free(EventLog* log);
void events_clear(EventLog* log);
void events_add(EventLog* log, bool verbose, int x, int y, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));

#endif
