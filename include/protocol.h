#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "world.h"

#define MAX_MESSAGE_SIZE (4u << 20)

typedef enum {
    MSG_HELLO = 1,
    MSG_WELCOME,
    MSG_COMMAND,
    MSG_REPLY,
    MSG_BYE,
    MSG_FRAME,
} MessageType;

typedef enum {
    ROLE_FORMATION = ACTOR_FORMATION,
    ROLE_CANNON = ACTOR_CANNON,
    ROLE_ALIEN = ACTOR_ALIEN,
    ROLE_OBSERVER,
} Role;

typedef struct {
    uint32_t type;
    uint32_t size;
} MessageHeader;

typedef struct {
    Role role;
} Hello;

typedef struct {
    bool accepted;
    int index;
    Config cfg;
    char reason[128];
} Welcome;

typedef struct {
    Command command;
    int tick;
} CommandHeader;

typedef struct {
    Formation formation;
    Cannon cannon;
    int aliens_alive;
    int shot_count;
} SnapshotHeader;

typedef struct {
    int index;
    Stats stats;
    union {
        Formation formation;
        Cannon cannon;
        Alien alien;
    } state;
} Reply;

typedef struct {
    char reason[128];
} Bye;

int net_listen(const char* host, const char* port);
int net_connect(const char* host, const char* port);
void net_peer_name(int fd, char* buf, size_t size);
void net_set_nodelay(int fd);

bool write_all(int fd, const void* buf, size_t size);
bool read_exact(int fd, void* buf, size_t size);
bool skip_bytes(int fd, size_t size);

bool send_message(int fd, MessageType type, const void* payload, size_t size);
bool send_message2(int fd, MessageType type, const void* first, size_t first_size,
                   const void* second, size_t second_size);
bool receive_header(int fd, MessageHeader* header);

#endif
