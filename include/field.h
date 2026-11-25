#ifndef FIELD_H
#define FIELD_H

#include "world.h"

typedef struct {
    void* ctx;
    bool (*run_phase)(void* ctx, World* world, Command command);
    void (*stop_actor)(void* ctx, World* world, int index);
    void (*publish)(void* ctx, World* world);
    void (*wait)(void* ctx, World* world, int ms);
} Backend;

void field_run(World* world, const Backend* backend);
void field_stop_all(World* world, const Backend* backend);
int outcome_exit_code(Outcome outcome);

#endif
