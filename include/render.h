#ifndef RENDER_H
#define RENDER_H

#include "world.h"

#include <stdio.h>

// поле после такта
void render_tick(const World* world);
// заголовок такта и строка состояния
void render_header(const World* world, FILE* out);
void render_status(const World* world, FILE* out);
// итог игры и статистика
void render_summary(const World* world, FILE* out);

#endif
