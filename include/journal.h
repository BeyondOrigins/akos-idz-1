#ifndef JOURNAL_H
#define JOURNAL_H

#include "world.h"

// Журнал игры в файле cfg.log_path: параметры, события каждого такта,
// а по завершении итог, статистика и сведения о потоках-участниках.
// Пишет только координатор поля, поэтому отдельная защита не нужна.

// открывает файл и пишет параметры; без журнала (log_path == NULL) ничего не делает
bool journal_open(World* world);
// события и состояние после такта
void journal_tick(const World* world);
// итог, статистика, таблица потоков; закрывает файл
void journal_close(World* world);

#endif
