#ifndef ACTORS_H
#define ACTORS_H

#include "world.h"

// создаёт потоки строя, пушки и всех пришельцев; false, если не удалось
// (уже созданные потоки при этом завершаются)
bool actors_start(World* world);
// раздаёт команду всем работающим участникам и ждёт, пока каждый её выполнит
void actors_run_phase(World* world, Command command);
// завершает поток участника и дожидается его окончания
void actors_stop(World* world, Actor* actor);
void actors_stop_all(World* world);
// освобождает семафоры и массив участников; потоки уже должны быть завершены
void actors_destroy(World* world);
// имя участника для вывода: "строй", "пушка", "пришелец #5 (стрелок)"
void actor_name(const World* world, const Actor* actor, char* buf, size_t size);

#endif
