#include "../include/events.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void events_init(EventLog* log) {
    log->items = NULL;
    log->count = 0;
    log->capacity = 0;
}

void events_free(EventLog* log) {
    free(log->items);
    events_init(log);
}

void events_clear(EventLog* log) {
    log->count = 0;
}

void events_add(EventLog* log, bool verbose, int x, int y, const char* fmt, ...) {
    if (log->count == log->capacity) {
        int capacity = log->capacity == 0 ? 32 : log->capacity * 2;
        Event* items = realloc(log->items, (size_t)capacity * sizeof(Event));
        if (items == NULL) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        log->items = items;
        log->capacity = capacity;
    }

    Event* event = &log->items[log->count++];
    event->verbose = verbose;
    event->x = x;
    event->y = y;

    va_list args;
    va_start(args, fmt);
    vsnprintf(event->text, sizeof(event->text), fmt, args);
    va_end(args);
}
