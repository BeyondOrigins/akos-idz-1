#ifndef RENDER_H
#define RENDER_H

#include "world.h"

#include <stdio.h>

// поле после такта
void render_tick(const World* world);
// итог игры и статистика
void render_summary(const World* world, FILE* out);
// таблица процессов-участников
void render_processes(const World* world, FILE* out);

#endif
