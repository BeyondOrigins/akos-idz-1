#include "../include/signals.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t stop_signal = 0;
static volatile sig_atomic_t forward_pid = 0;

void set_handler(int sig, void (*handler)(int)) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    // без SA_RESTART: сигнал прерывает sem_wait, read, open и pause
    action.sa_flags = 0;
    sigaction(sig, &action, NULL);
}

void on_stop(int sig) {
    if (stop_signal == 0) {
        stop_signal = sig;
    }
}

void signals_setup_coordinator(void) {
    set_handler(SIGINT, on_stop);
    set_handler(SIGTERM, on_stop);
    set_handler(SIGHUP, on_stop);
    // запись в канал, который закрыл упавший участник, не должна убивать координатора
    set_handler(SIGPIPE, SIG_IGN);
}

int signals_stop_signal(void) {
    return stop_signal;
}

void signals_setup_fork_child(void) {
    set_handler(SIGINT, SIG_IGN);
    set_handler(SIGHUP, SIG_IGN);
    set_handler(SIGTERM, SIG_DFL);
    set_handler(SIGPIPE, SIG_DFL);
}

// kill можно вызывать из обработчика сигнала
void on_participant_signal(int sig) {
    if (forward_pid > 0) {
        kill((pid_t)forward_pid, sig);
    } else if (stop_signal == 0) {
        stop_signal = sig;
    }
}

void signals_setup_participant(void) {
    set_handler(SIGINT, on_participant_signal);
    set_handler(SIGTERM, on_participant_signal);
    set_handler(SIGHUP, on_participant_signal);
    set_handler(SIGPIPE, SIG_IGN);
}

void signals_forward_to(pid_t coordinator) {
    forward_pid = (sig_atomic_t)coordinator;
}

bool signals_participant_stop(void) {
    return stop_signal != 0;
}
