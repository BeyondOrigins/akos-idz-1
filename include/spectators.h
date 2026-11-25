#ifndef SPECTATORS_H
#define SPECTATORS_H

#include <poll.h>
#include "protocol.h"

#define MAX_SPECTATORS 64

void spectators_init(void);
bool spectators_join(int fd, Welcome* welcome);
void spectators_prepare(struct pollfd* fds);
void spectators_service(const struct pollfd* fds);
void spectators_publish(const World* world);
void spectators_finish(const char* result);

#endif
