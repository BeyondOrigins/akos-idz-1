#include "../include/signals.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t stop_signal = 0;

void set_handler(int sig, void (*handler)(int)) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(sig, &action, NULL);
}

void on_stop(int sig) {
    if (stop_signal == 0) {
        stop_signal = sig;
    }
}

void signals_setup(void) {
    set_handler(SIGINT, on_stop);
    set_handler(SIGTERM, on_stop);
    set_handler(SIGHUP, on_stop);
    set_handler(SIGPIPE, SIG_IGN);
}

int signals_stop_signal(void) {
    return stop_signal;
}
