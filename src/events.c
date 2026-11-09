#include "../include/events.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/sync.h"

void events_clear(EventLog* log) {
    sem_lock(g_sems.events_lock);
    log->count = 0;
    log->dropped = 0;
    sem_unlock(g_sems.events_lock);
}

void events_append(EventLog* log, const Event* event) {
    sem_lock(g_sems.events_lock);
    if (log->count < MAX_EVENTS) {
        log->items[log->count] = *event;
        log->items[log->count].seq = log->count;
        log->count++;
    } else {
        log->dropped++;
    }
    sem_unlock(g_sems.events_lock);
}

void events_vadd(EventLog* log, int source, bool verbose, int x, int y, const char* fmt,
                 va_list args) {
    // текст готовим до захвата семафора, чтобы держать его поменьше
    Event event;
    event.verbose = verbose;
    event.x = x;
    event.y = y;
    event.source = source;
    event.seq = 0;
    vsnprintf(event.text, sizeof(event.text), fmt, args);
    events_append(log, &event);
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
    sem_lock(g_sems.events_lock);
    qsort(log->items, (size_t)log->count, sizeof(Event), compare_events);
    sem_unlock(g_sems.events_lock);
}
