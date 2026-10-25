#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "include/actors.h"
#include "include/config.h"
#include "include/field.h"
#include "include/journal.h"
#include "include/render.h"
#include "include/signals.h"

// код завершения: по сигналу принято 128 + номер сигнала
int exit_code(Outcome outcome) {
    switch (outcome) {
        case OUTCOME_INTERRUPTED:
            return 130;  // SIGINT
        case OUTCOME_TERMINATED:
            return 143;  // SIGTERM
        default:
            return EXIT_SUCCESS;
    }
}

int main(int argc, char** argv) {
    Config cfg;
    config_set_defaults(&cfg);
    switch (config_parse(&cfg, argc, argv)) {
        case CONFIG_HELP:
            config_print_usage(argv[0]);
            return EXIT_SUCCESS;
        case CONFIG_ERROR:
            fprintf(stderr, "Справка: %s --help\n", argv[0]);
            return EXIT_FAILURE;
        case CONFIG_OK:
            break;
    }

    // поток сигналов запускается первым: маску блокировки наследуют все остальные
    if (!signals_start(cfg.max_ticks > 0)) {
        return EXIT_FAILURE;
    }

    if (cfg.animate) {
        // копим кадр в буфере и выводим целиком, чтобы не мерцало
        setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    }
    config_print(&cfg, stdout);

    World world;
    world_init(&world, &cfg);
    if (!journal_open(&world) || !actors_start(&world)) {
        actors_destroy(&world);
        if (world.journal != NULL) {
            fclose(world.journal);
        }
        world_destroy(&world);
        signals_shutdown();
        return EXIT_FAILURE;
    }

    // ограничение по времени: через time_limit_sec секунд придёт SIGALRM
    if (cfg.time_limit_sec > 0) {
        alarm((unsigned int)cfg.time_limit_sec);
    }

    field_run(&world);  // по окончании все потоки-участники завершены
    alarm(0);
    signals_shutdown();

    render_summary(&world, stdout);
    journal_close(&world);
    if (cfg.log_path != NULL) {
        printf("Журнал сохранён в %s\n", cfg.log_path);
    }

    actors_destroy(&world);
    world_destroy(&world);
    return exit_code(world.outcome);
}
