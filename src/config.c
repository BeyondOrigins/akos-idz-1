#include "../include/config.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    OPT_WIDTH = 256,
    OPT_HEIGHT,
    OPT_ROWS,
    OPT_COLS,
    OPT_FORMATION_SPEED,
    OPT_FORMATION_PERIOD,
    OPT_DIRECTION,
    OPT_FIRE_RATE,
    OPT_ALIEN_SHOT_SPEED,
    OPT_SHOT_SPEED,
    OPT_AMMO,
    OPT_LIVES,
    OPT_STRATEGY,
    OPT_EXTENDED,
    OPT_SHIELD,
    OPT_TICKS,
    OPT_SEED,
    OPT_DELAY,
    OPT_ANIMATE,
    OPT_NO_ANIMATE,
    OPT_VERBOSE,
    OPT_HELP,
};

const struct option LONG_OPTIONS[] = {
    {"width", required_argument, NULL, OPT_WIDTH},
    {"height", required_argument, NULL, OPT_HEIGHT},
    {"rows", required_argument, NULL, OPT_ROWS},
    {"cols", required_argument, NULL, OPT_COLS},
    {"formation-speed", required_argument, NULL, OPT_FORMATION_SPEED},
    {"formation-period", required_argument, NULL, OPT_FORMATION_PERIOD},
    {"direction", required_argument, NULL, OPT_DIRECTION},
    {"fire-rate", required_argument, NULL, OPT_FIRE_RATE},
    {"alien-shot-speed", required_argument, NULL, OPT_ALIEN_SHOT_SPEED},
    {"shot-speed", required_argument, NULL, OPT_SHOT_SPEED},
    {"ammo", required_argument, NULL, OPT_AMMO},
    {"lives", required_argument, NULL, OPT_LIVES},
    {"strategy", required_argument, NULL, OPT_STRATEGY},
    {"extended", no_argument, NULL, OPT_EXTENDED},
    {"shield", required_argument, NULL, OPT_SHIELD},
    {"ticks", required_argument, NULL, OPT_TICKS},
    {"seed", required_argument, NULL, OPT_SEED},
    {"delay", required_argument, NULL, OPT_DELAY},
    {"animate", no_argument, NULL, OPT_ANIMATE},
    {"no-animate", no_argument, NULL, OPT_NO_ANIMATE},
    {"verbose", no_argument, NULL, OPT_VERBOSE},
    {"help", no_argument, NULL, OPT_HELP},
    {NULL, 0, NULL, 0},
};

const char* const STRATEGY_NAMES[] = {
    [STRATEGY_HUNTER] = "hunter",
    [STRATEGY_SWEEPER] = "sweeper",
    [STRATEGY_DODGER] = "dodger",
    [STRATEGY_RANDOM] = "random",
};

const char* strategy_name(Strategy strategy) {
    return STRATEGY_NAMES[strategy];
}

void config_set_defaults(Config* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->width = 30;
    cfg->height = 16;
    cfg->alien_rows = 3;
    cfg->alien_cols = 8;
    cfg->formation_speed = 1;
    cfg->formation_period = 2;
    cfg->formation_dir = 1;
    cfg->alien_fire_rate = 4;
    cfg->alien_shot_speed = 1;
    cfg->cannon_shot_speed = 2;
    cfg->cannon_ammo = 60;
    cfg->cannon_lives = 3;
    cfg->strategy = STRATEGY_HUNTER;
    cfg->max_ticks = 300;
    cfg->seed = 1;
    cfg->animate = isatty(STDOUT_FILENO) != 0;
    cfg->delay_ms = DELAY_AUTO;
}

void config_print_usage(const char* prog) {
    printf(
        "Использование: %s [параметры]\n"
        "\n"
        "Поле и строй:\n"
        "  --width N             ширина поля (8..100, по умолчанию 30)\n"
        "  --height N            высота поля (8..50, по умолчанию 16)\n"
        "  --rows N              число рядов пришельцев (по умолчанию 3)\n"
        "  --cols N              число столбцов пришельцев (по умолчанию 8)\n"
        "  --formation-speed N   клеток за один ход строя (по умолчанию 1)\n"
        "  --formation-period N  строй ходит раз в N тактов (по умолчанию 2)\n"
        "  --direction left|right  начальное направление строя (right)\n"
        "\n"
        "Стрельба пришельцев:\n"
        "  --fire-rate P         вероятность выстрела за такт, %% (по умолчанию 4)\n"
        "  --alien-shot-speed N  скорость снарядов пришельцев (по умолчанию 1)\n"
        "\n"
        "Пушка:\n"
        "  --shot-speed N        скорость снарядов пушки (по умолчанию 2)\n"
        "  --ammo N              запас снарядов (по умолчанию 60)\n"
        "  --lives N             число попаданий до уничтожения (по умолчанию 3)\n"
        "  --strategy NAME       hunter | sweeper | dodger | random (hunter)\n"
        "\n"
        "Расширенный режим:\n"
        "  --extended            несколько типов пришельцев и укрытия\n"
        "  --shield X,Y,W,HP     полоса укрытия из W клеток в строке Y со столбца X\n"
        "                        прочностью HP (1..9); можно указать до %d раз,\n"
        "                        включает расширенный режим\n"
        "\n"
        "Моделирование и вывод:\n"
        "  --ticks N             предельное число тактов (по умолчанию 300)\n"
        "  --seed N              зерно генератора случайных чисел (по умолчанию 1)\n"
        "  --delay MS            пауза между тактами, мс (по умолчанию %d с анимацией,\n"
        "                        0 без неё)\n"
        "  --animate             перерисовывать поле на месте (по умолчанию в терминале)\n"
        "  --no-animate          печатать кадры подряд (по умолчанию при выводе в файл)\n"
        "  --verbose             показывать перемещение каждого снаряда\n"
        "  --help                эта справка\n",
        prog, MAX_SHIELDS, DEFAULT_ANIMATION_DELAY_MS);
}

bool parse_int(const char* text, const char* name, int min, int max, int* out) {
    char* end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max) {
        fprintf(stderr, "Ошибка: --%s ожидает целое число от %d до %d, получено \"%s\"\n",
                name, min, max, text);
        return false;
    }
    *out = (int)value;
    return true;
}

bool parse_seed(const char* text, uint64_t* out) {
    char* end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || text[0] == '-') {
        fprintf(stderr, "Ошибка: --seed ожидает неотрицательное целое, получено \"%s\"\n", text);
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

bool parse_direction(const char* text, int* out) {
    if (strcmp(text, "right") == 0) {
        *out = 1;
        return true;
    }
    if (strcmp(text, "left") == 0) {
        *out = -1;
        return true;
    }
    fprintf(stderr, "Ошибка: --direction ожидает left или right, получено \"%s\"\n", text);
    return false;
}

bool parse_strategy(const char* text, Strategy* out) {
    size_t count = sizeof(STRATEGY_NAMES) / sizeof(STRATEGY_NAMES[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(text, STRATEGY_NAMES[i]) == 0) {
            *out = (Strategy)i;
            return true;
        }
    }
    fprintf(stderr, "Ошибка: неизвестная стратегия \"%s\"\n", text);
    return false;
}

bool parse_shield(Config* cfg, const char* text) {
    if (cfg->shield_count >= MAX_SHIELDS) {
        fprintf(stderr, "Ошибка: можно задать не более %d укрытий\n", MAX_SHIELDS);
        return false;
    }
    ShieldSpec spec;
    char tail;
    if (sscanf(text, "%d,%d,%d,%d%c", &spec.x, &spec.y, &spec.width, &spec.hp, &tail) != 4) {
        fprintf(stderr, "Ошибка: --shield ожидает X,Y,W,HP, получено \"%s\"\n", text);
        return false;
    }
    cfg->shields[cfg->shield_count++] = spec;
    cfg->extended = true;
    return true;
}

void add_default_shields(Config* cfg) {
    int width = cfg->width / 8 < 2 ? 2 : cfg->width / 8;
    for (int i = 0; i < 3; i++) {
        ShieldSpec* spec = &cfg->shields[cfg->shield_count++];
        spec->x = (i + 1) * cfg->width / 4 - width / 2;
        spec->y = cfg->height - 3;
        spec->width = width;
        spec->hp = 3;
    }
}

bool config_validate(const Config* cfg) {
    int formation_width = (cfg->alien_cols - 1) * ALIEN_SPACING_X + 1;
    if (formation_width > cfg->width) {
        fprintf(stderr, "Ошибка: строй из %d столбцов (ширина %d) не помещается в поле шириной %d\n",
                cfg->alien_cols, formation_width, cfg->width);
        return false;
    }
    if (cfg->alien_rows > cfg->height - 5) {
        fprintf(stderr, "Ошибка: для поля высотой %d допустимо не более %d рядов пришельцев\n",
                cfg->height, cfg->height - 5);
        return false;
    }
    if (cfg->alien_shot_speed >= cfg->height || cfg->cannon_shot_speed >= cfg->height) {
        fprintf(stderr, "Ошибка: скорость снарядов должна быть меньше высоты поля\n");
        return false;
    }
    if (cfg->formation_speed >= cfg->width) {
        fprintf(stderr, "Ошибка: скорость строя должна быть меньше ширины поля\n");
        return false;
    }

    int formation_bottom = ALIEN_TOP_ROW + cfg->alien_rows - 1;
    for (int i = 0; i < cfg->shield_count; i++) {
        const ShieldSpec* spec = &cfg->shields[i];
        bool fits = spec->width >= 1 && spec->x >= 0 && spec->x + spec->width <= cfg->width;
        bool between = spec->y > formation_bottom && spec->y <= cfg->height - 2;
        if (!fits || !between || spec->hp < 1 || spec->hp > 9) {
            fprintf(stderr,
                    "Ошибка: укрытие %d,%d,%d,%d некорректно: нужно 0 <= X, X+W <= %d, "
                    "%d <= Y <= %d, 1 <= HP <= 9\n",
                    spec->x, spec->y, spec->width, spec->hp, cfg->width, formation_bottom + 1,
                    cfg->height - 2);
            return false;
        }
    }
    return true;
}

ConfigStatus config_parse(Config* cfg, int argc, char** argv) {
    int opt;
    opterr = 0;
    while ((opt = getopt_long(argc, argv, "h", LONG_OPTIONS, NULL)) != -1) {
        bool ok = true;
        switch (opt) {
            case OPT_WIDTH:
                ok = parse_int(optarg, "width", 8, 100, &cfg->width);
                break;
            case OPT_HEIGHT:
                ok = parse_int(optarg, "height", 8, 50, &cfg->height);
                break;
            case OPT_ROWS:
                ok = parse_int(optarg, "rows", 1, 45, &cfg->alien_rows);
                break;
            case OPT_COLS:
                ok = parse_int(optarg, "cols", 1, 50, &cfg->alien_cols);
                break;
            case OPT_FORMATION_SPEED:
                ok = parse_int(optarg, "formation-speed", 1, 99, &cfg->formation_speed);
                break;
            case OPT_FORMATION_PERIOD:
                ok = parse_int(optarg, "formation-period", 1, 1000, &cfg->formation_period);
                break;
            case OPT_DIRECTION:
                ok = parse_direction(optarg, &cfg->formation_dir);
                break;
            case OPT_FIRE_RATE:
                ok = parse_int(optarg, "fire-rate", 0, 100, &cfg->alien_fire_rate);
                break;
            case OPT_ALIEN_SHOT_SPEED:
                ok = parse_int(optarg, "alien-shot-speed", 1, 49, &cfg->alien_shot_speed);
                break;
            case OPT_SHOT_SPEED:
                ok = parse_int(optarg, "shot-speed", 1, 49, &cfg->cannon_shot_speed);
                break;
            case OPT_AMMO:
                ok = parse_int(optarg, "ammo", 0, INT_MAX, &cfg->cannon_ammo);
                break;
            case OPT_LIVES:
                ok = parse_int(optarg, "lives", 1, 99, &cfg->cannon_lives);
                break;
            case OPT_STRATEGY:
                ok = parse_strategy(optarg, &cfg->strategy);
                break;
            case OPT_EXTENDED:
                cfg->extended = true;
                break;
            case OPT_SHIELD:
                ok = parse_shield(cfg, optarg);
                break;
            case OPT_TICKS:
                ok = parse_int(optarg, "ticks", 1, INT_MAX, &cfg->max_ticks);
                break;
            case OPT_SEED:
                ok = parse_seed(optarg, &cfg->seed);
                break;
            case OPT_DELAY:
                ok = parse_int(optarg, "delay", 0, 60000, &cfg->delay_ms);
                break;
            case OPT_ANIMATE:
                cfg->animate = true;
                break;
            case OPT_NO_ANIMATE:
                cfg->animate = false;
                break;
            case OPT_VERBOSE:
                cfg->verbose = true;
                break;
            case 'h':
            case OPT_HELP:
                return CONFIG_HELP;
            default:
                fprintf(stderr,
                        "Ошибка: неизвестный параметр или пропущено значение: \"%s\"\n",
                        argv[optind - 1]);
                return CONFIG_ERROR;
        }
        if (!ok) {
            return CONFIG_ERROR;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "Ошибка: лишний аргумент \"%s\"\n", argv[optind]);
        return CONFIG_ERROR;
    }

    if (cfg->extended && cfg->shield_count == 0) {
        add_default_shields(cfg);
    }
    if (cfg->delay_ms == DELAY_AUTO) {
        cfg->delay_ms = cfg->animate ? DEFAULT_ANIMATION_DELAY_MS : 0;
    }
    return config_validate(cfg) ? CONFIG_OK : CONFIG_ERROR;
}

void config_print(const Config* cfg) {
    printf("Параметры моделирования:\n");
    printf("  поле: %dx%d, предел тактов: %d, зерно: %llu\n", cfg->width, cfg->height,
           cfg->max_ticks, (unsigned long long)cfg->seed);
    printf("  строй: %d ряд(ов) x %d столбц(ов), скорость %d раз в %d такт(ов), направление %s\n",
           cfg->alien_rows, cfg->alien_cols, cfg->formation_speed, cfg->formation_period,
           cfg->formation_dir > 0 ? "вправо" : "влево");
    printf("  пришельцы: вероятность выстрела %d%%, скорость снаряда %d\n", cfg->alien_fire_rate,
           cfg->alien_shot_speed);
    printf("  пушка: стратегия %s, снарядов %d, скорость снаряда %d, жизней %d\n",
           strategy_name(cfg->strategy), cfg->cannon_ammo, cfg->cannon_shot_speed,
           cfg->cannon_lives);
    printf("  режим: %s\n", cfg->extended ? "расширенный" : "базовый");
    for (int i = 0; i < cfg->shield_count; i++) {
        const ShieldSpec* spec = &cfg->shields[i];
        printf("  укрытие: строка %d, столбцы %d..%d, прочность %d\n", spec->y, spec->x,
               spec->x + spec->width - 1, spec->hp);
    }
    printf("\n");
}
