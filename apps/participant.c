#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/protocol.h"
#include "../include/signals.h"
#include "../include/world.h"

#ifndef ROLE
#error "ROLE не задан"
#endif

static World local;
static int server_fd = -1;
static char name[64];
static int last_hp = -1;

bool read_snapshot(void) {
    SnapshotHeader header;
    if (!read_exact(server_fd, &header, sizeof(header))) {
        return false;
    }
    if (header.shot_count < 0 || header.shot_count > MAX_SHOTS) {
        return false;
    }
    local.formation = header.formation;
    local.cannon = header.cannon;
    local.aliens_alive = header.aliens_alive;
    local.shot_count = header.shot_count;
    return read_exact(server_fd, local.aliens, sizeof(Alien) * (size_t)local.alien_count) &&
           read_exact(server_fd, local.shots, sizeof(Projectile) * (size_t)local.shot_count) &&
           read_exact(server_fd, local.shields, (size_t)(local.width * local.height));
}

const char* step_text(int dx) {
    return dx < 0 ? "влево" : dx > 0 ? "вправо" : "на месте";
}

void report_plan(int index) {
    const ActorSlot* slot = &local.actors[index];
    if (slot->kind == ACTOR_FORMATION) {
        const Formation* formation = &local.formation;
        if (formation->dy != 0) {
            printf("Такт %d: пришельцев в строю %d, строй у границы — решение: спуститься и "
                   "развернуться\n",
                   local.tick, local.aliens_alive);
        } else if (formation->dx != 0) {
            printf("Такт %d: пришельцев в строю %d — решение: сдвиг %s на %d\n", local.tick,
                   local.aliens_alive, formation->dx > 0 ? "вправо" : "влево",
                   formation->dx > 0 ? formation->dx : -formation->dx);
        } else {
            printf("Такт %d: пришельцев в строю %d — решение: стоять (ход через %d такт(ов))\n",
                   local.tick, local.aliens_alive, formation->ticks_until_move - 1);
        }
    } else if (slot->kind == ACTOR_CANNON) {
        const Cannon* cannon = &local.cannon;
        printf("Такт %d: пушка в столбце %d, жизней %d, снарядов %d, пришельцев %d, снарядов в "
               "полёте %d — решение: %s, стрелять: %s\n",
               local.tick, cannon->x, cannon->lives, cannon->ammo, local.aliens_alive,
               local.shot_count, step_text(cannon->dx), cannon->wants_fire ? "да" : "нет");
    } else {
        const Alien* alien = &local.aliens[slot->alien_index];
        if (last_hp >= 0 && alien->hp < last_hp) {
            printf("Такт %d: получено попадание, осталось прочности %d\n", local.tick, alien->hp);
        }
        last_hp = alien->hp;
        if (alien->wants_fire) {
            printf("Такт %d: позиция (%d,%d) — решение: стрелять\n", local.tick, alien->x,
                   alien->y);
        }
    }
}

void report_move(int index) {
    const ActorSlot* slot = &local.actors[index];
    if (slot->kind == ACTOR_CANNON && local.cannon.fired) {
        printf("Такт %d: выстрел из столбца %d, осталось снарядов %d\n", local.tick,
               local.cannon.x, local.cannon.ammo);
    } else if (slot->kind == ACTOR_ALIEN && local.aliens[slot->alien_index].fired) {
        const Alien* alien = &local.aliens[slot->alien_index];
        printf("Такт %d: выстрел из (%d,%d)\n", local.tick, alien->x, alien->y + 1);
    }
}

bool send_reply(int index) {
    Reply reply;
    memset(&reply, 0, sizeof(reply));
    reply.index = index;
    reply.stats = local.stats;
    const ActorSlot* slot = &local.actors[index];
    switch (slot->kind) {
        case ACTOR_FORMATION:
            reply.state.formation = local.formation;
            break;
        case ACTOR_CANNON:
            reply.state.cannon = local.cannon;
            break;
        case ACTOR_ALIEN:
            reply.state.alien = local.aliens[slot->alien_index];
            break;
    }
    return send_message(server_fd, MSG_REPLY, &reply, sizeof(reply));
}

int lost_connection(void) {
    if (signals_stop_signal() != 0) {
        printf("%s: клиент остановлен пользователем, соединение закрыто\n", name);
        return 128 + signals_stop_signal();
    }
    printf("%s: соединение с сервером потеряно\n", name);
    return EXIT_FAILURE;
}

int command_loop(int index) {
    for (;;) {
        fflush(stdout);
        MessageHeader header;
        if (!receive_header(server_fd, &header)) {
            return lost_connection();
        }
        if (header.type == MSG_BYE) {
            Bye bye;
            if (header.size == sizeof(bye) && read_exact(server_fd, &bye, sizeof(bye))) {
                printf("%s: сервер завершает работу: %s\n", name, bye.reason);
            }
            return EXIT_SUCCESS;
        }
        if (header.type != MSG_COMMAND || header.size < sizeof(CommandHeader)) {
            if (!skip_bytes(server_fd, header.size)) {
                return lost_connection();
            }
            continue;
        }
        CommandHeader command;
        if (!read_exact(server_fd, &command, sizeof(command))) {
            return lost_connection();
        }
        local.tick = command.tick;
        if (command.command == COMMAND_STOP) {
            printf("Такт %d: сервер прислал команду завершения, %s выходит из игры\n",
                   command.tick, name);
            return EXIT_SUCCESS;
        }
        bool ok = command.command == COMMAND_PLAN
                      ? read_snapshot()
                      : read_exact(server_fd, &local.formation, sizeof(local.formation));
        if (!ok) {
            return lost_connection();
        }
        events_clear(&local.events);
        memset(&local.stats, 0, sizeof(local.stats));
        actor_do(&local, index, command.command);
        if (command.command == COMMAND_PLAN) {
            report_plan(index);
        } else {
            for (int i = 0; i < local.events.count; i++) {
                puts(local.events.items[i].text);
            }
            report_move(index);
        }
        if (!send_reply(index)) {
            return lost_connection();
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Использование: %s <IP сервера> <порт>\n", argv[0]);
        return EXIT_FAILURE;
    }
    signals_setup();
    snprintf(name, sizeof(name), "%s", actor_kind_name(ROLE));
    server_fd = net_connect(argv[1], argv[2]);
    if (server_fd < 0) {
        return EXIT_FAILURE;
    }
    Hello hello = {(Role)ROLE};
    Welcome welcome;
    MessageHeader header;
    if (!send_message(server_fd, MSG_HELLO, &hello, sizeof(hello)) ||
        !receive_header(server_fd, &header) || header.type != MSG_WELCOME ||
        header.size != sizeof(welcome) || !read_exact(server_fd, &welcome, sizeof(welcome))) {
        fprintf(stderr, "Ошибка: сервер %s:%s не ответил на приветствие\n", argv[1], argv[2]);
        close(server_fd);
        return EXIT_FAILURE;
    }
    if (!welcome.accepted) {
        fprintf(stderr, "Сервер отказал: %s\n", welcome.reason);
        close(server_fd);
        return EXIT_FAILURE;
    }
    world_init(&local, &welcome.cfg);
    actor_name(&local, welcome.index, name, sizeof(name));
    printf("Подключён к серверу %s:%s как %s (место %d)\n", argv[1], argv[2], name,
           welcome.index);
    int code = command_loop(welcome.index);
    fflush(stdout);
    close(server_fd);
    return code;
}
