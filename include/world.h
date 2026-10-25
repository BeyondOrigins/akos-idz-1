#ifndef WORLD_H
#define WORLD_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "config.h"
#include "events.h"
#include "rng.h"

// проверка инварианта
#define INVARIANT(cond)                                                              \
    do {                                                                             \
        if (!(cond)) {                                                               \
            world_invariant_failed(#cond, __FILE__, __LINE__);                       \
        }                                                                            \
    } while (0)

typedef enum {
    ALIEN_GRUNT,       // рядовой
    ALIEN_SHOOTER,     // стрелок
    ALIEN_TANK,        // танк
    ALIEN_TYPE_COUNT,  // количество типов
} AlienType;

typedef struct {
    const char* name;
    char glyph;           // каким символом рисуется
    int hp;
    int score;
    int fire_multiplier;  // во сколько раз чаще стреляет
} AlienTypeInfo;

extern const AlienTypeInfo ALIEN_TYPES[ALIEN_TYPE_COUNT];

typedef struct {
    int id;
    int col;  // столбец в строю
    AlienType type;
    int x;
    int y;
    int hp;
    bool alive;
    Rng rng;

    bool wants_fire;  // решение на этот такт
    bool fired;       // выстрел действительно состоялся
    int shots_fired;  // всего выстрелов за игру
} Alien;

// координатор строя
typedef struct {
    int dir;               // +1 вправо, -1 влево
    int ticks_until_move;  // 1: ходит уже в этом такте

    // план на этот такт
    int dx;
    int dy;
    int next_dir;
} Formation;

typedef struct {
    int x;
    int y;
    int lives;
    int ammo;
    int sweep_dir;  // куда сейчас едет sweeper
    Rng rng;

    // план на этот такт
    int dx;
    bool wants_fire;
    bool fired;  // выстрел действительно состоялся
} Cannon;

typedef enum {
    OWNER_CANNON,
    OWNER_ALIEN,
} OwnerKind;

typedef struct {
    int id;
    OwnerKind owner;
    int owner_id;               // id пришельца, для пушки 0
    int x;
    int y;
    int dir;                    // -1 вверх, +1 вниз
    int speed;
    bool active;
    bool fresh;                 // только что выпущен, в этом такте не летит
    bool moved;                 // сдвинулся на текущем шаге
    int tick_start_y;           // где был в начале такта
    unsigned long moved_stamp;  // защита от двойной обработки за шаг
} Projectile;

typedef enum {
    OUTCOME_RUNNING,           // в процессе игры
    OUTCOME_VICTORY,           // победа
    OUTCOME_CANNON_DESTROYED,  // проигрыш - пушка уничтожена
    OUTCOME_INVASION,          // проигрыш - пришелец добрался до нижней границы
    OUTCOME_TIMEOUT,           // исчерпано число тактов
    OUTCOME_TIME_LIMIT,        // истекло время моделирования (SIGALRM)
    OUTCOME_INTERRUPTED,       // программа прервана через Ctrl + C (SIGINT)
    OUTCOME_TERMINATED,        // программа остановлена сигналом SIGTERM
} Outcome;

// что координатор поручает потоку-участнику
typedef enum {
    COMMAND_PLAN,  // фаза формирования действий
    COMMAND_MOVE,  // фаза перемещения участников
    COMMAND_STOP,  // завершить поток
} Command;

typedef enum {
    ACTOR_FORMATION,
    ACTOR_CANNON,
    ACTOR_ALIEN,
} ActorKind;

// места участников в массиве потоков: строй, пушка, затем пришельцы
#define ACTOR_FORMATION_INDEX 0
#define ACTOR_CANNON_INDEX 1
#define ACTOR_ALIENS_BASE 2

struct World;

// поток-участник модели
typedef struct {
    ActorKind kind;
    int alien_index;       // для пришельца: индекс в массиве aliens
    struct World* world;
    pthread_t thread;
    sem_t* go;             // координатор разрешает выполнить command
    Command command;
    bool running;          // поток создан и ещё не завершён
    int phases;            // сколько фаз отработал
    int stop_tick;         // на каком такте завершён
} Actor;

// статистика игры; её пополняют несколько потоков, поэтому
// изменения делаются только под мьютексом stats_lock
typedef struct {
    int formation_steps;       // сдвигов строя вбок
    int formation_descents;    // спусков строя
    int cannon_moves;
    int cannon_shots;
    int cannon_hits;           // попаданий снарядов пушки в пришельцев
    int alien_shots;
    int cannon_damage;         // попаданий снарядов пришельцев в пушку
    int shield_hits;
    int shield_cells_destroyed;
    int shot_collisions;       // столкновений встречных снарядов
    int shots_left_field;
    int kills[ALIEN_TYPE_COUNT];
} Stats;

typedef struct World {
    Config cfg;
    int width;
    int height;

    int tick;
    int score;
    Outcome outcome;

    Formation formation;
    Alien* aliens;
    int alien_count;
    int aliens_alive;
    Cannon cannon;

    int* shields;              // прочность укрытия в каждой клетке, 0 = пусто

    Projectile* shots;
    int shot_count;
    int shot_capacity;
    int next_shot_id;
    unsigned long step_stamp;  // номер текущего шага снарядов

    EventLog events;

    // потоки-участники и синхронизация фаз такта
    Actor* actors;
    int actor_count;
    int actors_running;
    sem_t* phase_done;  // участник сообщает, что фаза выполнена
    bool tick_limit_enabled;  // предел тактов действует (SIGUSR2 переключает)

    Stats stats;
    pthread_mutex_t stats_lock;

    FILE* journal;  // файл журнала или NULL
    struct timespec started_at;
    double elapsed_sec;
} World;

void* checked_calloc(size_t count, size_t size);

void world_init(World* world, const Config* cfg);
void world_destroy(World* world);

bool world_in_bounds(const World* world, int x, int y);
Alien* world_alien_at(World* world, int x, int y);
int* world_shield_at(World* world, int x, int y);
bool world_column_has_shield(const World* world, int x);

Projectile* world_add_shot(World* world, OwnerKind owner, int owner_id, int x, int y, int dir,
                           int speed);
void world_remove_inactive_shots(World* world);

void world_check_invariants(const World* world);
void world_invariant_failed(const char* cond, const char* file, int line);

#endif
