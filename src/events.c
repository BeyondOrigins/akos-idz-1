#include "../include/events.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/sync.h"

void events_init(EventLog* log) {
    log->items = NULL;
    log->count = 0;
    log->capacity = 0;
    mutex_init(&log->lock);
}

void events_free(EventLog* log) {
    free(log->items);
    log->items = NULL;
    log->count = 0;
    log->capacity = 0;
    mutex_destroy(&log->lock);
}

void events_clear(EventLog* log) {
    mutex_lock(&log->lock);
    log->count = 0;
    mutex_unlock(&log->lock);
}

void events_vadd(EventLog* log, int source, bool verbose, int x, int y, const char* fmt,
                 va_list args) {
    // текст готовим до захвата мьютекса, чтобы держать его поменьше
    Event event;
    event.verbose = verbose;
    event.x = x;
    event.y = y;
    event.source = source;
    vsnprintf(event.text, sizeof(event.text), fmt, args);

    mutex_lock(&log->lock);
    // место кончилось, удваиваем
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
    event.seq = log->count;
    log->items[log->count++] = event;
    mutex_unlock(&log->lock);
}

void events_add(EventLog* log, bool verbose, int x, int y, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    events_vadd(log, EVENT_SOURCE_FIELD, verbose, x, y, fmt, args);
    va_end(args);
}

void events_add_from(EventLog* log, int source, bool verbose, int x, int y, const char* fmt,
                     ...) {
    va_list args;
    va_start(args, fmt);
    events_vadd(log, source, verbose, x, y, fmt, args);
    va_end(args);
}

int compare_events(const void* a, const void* b) {
    const Event* left = a;
    const Event* right = b;
    if (left->source != right->source) {
        return left->source < right->source ? -1 : 1;
    }
    return (left->seq > right->seq) - (left->seq < right->seq);
}

void events_sort(EventLog* log) {
    mutex_lock(&log->lock);
    qsort(log->items, (size_t)log->count, sizeof(Event), compare_events);
    mutex_unlock(&log->lock);
}
