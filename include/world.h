#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>
#include <sys/types.h>
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

// снарядов в полёте одновременно: стреляет только нижний пришелец в столбце
// (не больше 50 за такт), снаряд живёт не дольше высоты поля (50 тактов)
#define MAX_SHOTS 4096

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
    OUTCOME_INTERRUPTED,       // программа прервана через Ctrl + C (SIGINT)
    OUTCOME_TERMINATED,        // программа остановлена сигналом SIGTERM или SIGHUP
    OUTCOME_ACTOR_LOST,        // процесс-участник аварийно завершился
} Outcome;

// что координатор поручает процессу-участнику
typedef enum {
    COMMAND_PLAN,  // фаза формирования действий
    COMMAND_MOVE,  // фаза перемещения участников
    COMMAND_STOP,  // завершить процесс
} Command;

typedef enum {
    ACTOR_FORMATION,
    ACTOR_CANNON,
    ACTOR_ALIEN,
} ActorKind;

// места участников в таблице процессов: строй, пушка, затем пришельцы
#define ACTOR_FORMATION_INDEX 0
#define ACTOR_CANNON_INDEX 1
#define ACTOR_ALIENS_BASE 2

// процесс-участник модели
typedef struct {
    ActorKind kind;
    int alien_index;  // для пришельца: индекс в массиве aliens
    Command command;  // последняя команда координатора
    bool joined;      // процесс подключился к игре
    bool running;     // процесс работает и получает команды
    pid_t pid;
    int phases;       // сколько команд выполнил
    int stop_tick;    // на каком такте завершён
} ActorSlot;

// статистика игры; её пополняют несколько процессов, поэтому изменения
// делаются только под семафором g_sems.stats_lock
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

// Состояние модели. В программах с разделяемой памятью структура целиком
// лежит в ней, поэтому внутри нет указателей — только массивы
typedef struct {
    Config cfg;
    int width;
    int height;

    int tick;
    int score;
    Outcome outcome;

    Formation formation;
    Alien aliens[MAX_ALIENS];
    int alien_count;
    int aliens_alive;
    Cannon cannon;

    unsigned char shields[MAX_CELLS];  // прочность укрытия в каждой клетке, 0 = пусто

    Projectile shots[MAX_SHOTS];
    int shot_count;
    int next_shot_id;
    unsigned long step_stamp;  // номер текущего шага снарядов

    EventLog events;

    // процессы-участники
    ActorSlot actors[MAX_ACTORS];
    int actor_count;
    int actors_running;
    pid_t coordinator_pid;

    Stats stats;

    struct timespec started_at;
    double elapsed_sec;
} World;

void world_init(World* world, const Config* cfg);

bool world_in_bounds(const World* world, int x, int y);
Alien* world_alien_at(World* world, int x, int y);
unsigned char* world_shield_at(World* world, int x, int y);
bool world_column_has_shield(const World* world, int x);

Projectile* world_add_shot(World* world, OwnerKind owner, int owner_id, int x, int y, int dir,
                           int speed);
void world_remove_inactive_shots(World* world);

void world_check_invariants(const World* world);
void world_invariant_failed(const char* cond, const char* file, int line);

void stats_add(Stats* total, const Stats* delta);

// участники
const char* actor_kind_name(ActorKind kind);
// имя для вывода: "строй", "пушка", "пришелец #5 (стрелок)"
void actor_name(const World* world, int index, char* buf, size_t size);
// выполнить команду PLAN или MOVE за участника index (вызывается в его процессе)
void actor_do(World* world, int index, Command command);

#endif
