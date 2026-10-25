#include "../include/signals.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/sync.h"

// состояние, общее для потока сигналов и координатора поля;
// все поля, кроме thread и set, читаются и пишутся только под lock
static struct {
    pthread_mutex_t lock;
    pthread_t thread;
    sigset_t set;  // сигналы, которые ждёт поток

    bool shutdown;
    bool has_tick_limit;
    bool tick_limit_enabled;
    bool report_requested;
    StopReason stop;
    int received;

    // сообщения для журнала событий: поток сигналов кладёт, координатор забирает
    char notes[MAX_SIGNAL_NOTES][SIGNAL_NOTE_SIZE];
    int note_count;
} control = {.lock = PTHREAD_MUTEX_INITIALIZER};

static const int HANDLED_SIGNALS[] = {SIGINT, SIGTERM, SIGALRM, SIGUSR1, SIGUSR2};

// вызывается под control.lock
static void add_note(const char* text) {
    if (control.note_count < MAX_SIGNAL_NOTES) {
        snprintf(control.notes[control.note_count++], SIGNAL_NOTE_SIZE, "%s", text);
    }
}

// вызывается под control.lock; первая причина остановки остаётся в силе
static void request_stop(StopReason reason) {
    if (control.stop == STOP_NONE) {
        control.stop = reason;
    }
}

// вызывается под control.lock
static void handle_signal(int sig) {
    switch (sig) {
        case SIGINT:
            if (control.stop != STOP_NONE) {
                // остановка уже запрошена (Ctrl+C, SIGTERM или SIGALRM), а программа
                // ещё работает: видимо, не отвечает, выходим сразу.
                // write, а не printf: stdout может быть занят координатором
                static const char msg[] = "\nПовторный SIGINT: аварийное завершение\n";
                ssize_t written = write(STDERR_FILENO, msg, sizeof(msg) - 1);
                (void)written;
                _exit(130);
            }
            request_stop(STOP_INTERRUPT);
            break;
        case SIGTERM:
            request_stop(STOP_TERMINATE);
            break;
        case SIGALRM:
            request_stop(STOP_TIME_LIMIT);
            break;
        case SIGUSR1:
            control.report_requested = true;
            add_note("Получен SIGUSR1: запрошен отчёт о состоянии");
            break;
        case SIGUSR2:
            if (!control.has_tick_limit) {
                add_note("Получен SIGUSR2: предел тактов не задан (--ticks 0), менять нечего");
            } else if (control.tick_limit_enabled) {
                control.tick_limit_enabled = false;
                add_note("Получен SIGUSR2: предел тактов снят, игра идёт до исхода или "
                         "прерывания");
            } else {
                control.tick_limit_enabled = true;
                add_note("Получен SIGUSR2: предел тактов восстановлен");
            }
            break;
    }
}

static void* signal_thread_main(void* arg) {
    (void)arg;
    for (;;) {
        int sig;
        int err = sigwait(&control.set, &sig);
        if (err != 0) {
            if (err == EINTR) {
                continue;
            }
            fprintf(stderr, "Ошибка: sigwait: %s\n", strerror(err));
            abort();
        }
        mutex_lock(&control.lock);
        if (control.shutdown) {
            mutex_unlock(&control.lock);
            break;
        }
        control.received++;
        handle_signal(sig);
        mutex_unlock(&control.lock);
    }
    return NULL;
}

bool signals_start(bool has_tick_limit) {
    control.has_tick_limit = has_tick_limit;
    control.tick_limit_enabled = has_tick_limit;

    sigemptyset(&control.set);
    size_t count = sizeof(HANDLED_SIGNALS) / sizeof(HANDLED_SIGNALS[0]);
    for (size_t i = 0; i < count; i++) {
        sigaddset(&control.set, HANDLED_SIGNALS[i]);
    }

    // сначала блокируем: иначе сигнал, пришедший до блокировки, выполнил бы
    // действие по умолчанию и завершил процесс. Маску наследуют все потоки,
    // созданные после этого вызова
    int err = pthread_sigmask(SIG_BLOCK, &control.set, NULL);
    if (err != 0) {
        fprintf(stderr, "Ошибка: pthread_sigmask: %s\n", strerror(err));
        return false;
    }

    // фоновый процесс в неинтерактивной оболочке наследует игнорирование
    // SIGINT; возвращаем действие по умолчанию, чтобы sigwait его получал
    for (size_t i = 0; i < count; i++) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        if (sigaction(HANDLED_SIGNALS[i], &action, NULL) != 0) {
            perror("Ошибка: sigaction");
            return false;
        }
    }

    err = pthread_create(&control.thread, NULL, signal_thread_main, NULL);
    if (err != 0) {
        fprintf(stderr, "Ошибка: не удалось создать поток сигналов: %s\n", strerror(err));
        return false;
    }
    return true;
}

void signals_shutdown(void) {
    mutex_lock(&control.lock);
    control.shutdown = true;
    mutex_unlock(&control.lock);
    // будим поток: сигнал заблокирован, поэтому он достанется sigwait
    pthread_kill(control.thread, SIGUSR1);
    pthread_join(control.thread, NULL);
}

StopReason signals_stop_reason(void) {
    mutex_lock(&control.lock);
    StopReason stop = control.stop;
    mutex_unlock(&control.lock);
    return stop;
}

bool signals_tick_limit_enabled(void) {
    mutex_lock(&control.lock);
    bool enabled = control.tick_limit_enabled;
    mutex_unlock(&control.lock);
    return enabled;
}

bool signals_take_report_request(void) {
    mutex_lock(&control.lock);
    bool requested = control.report_requested;
    control.report_requested = false;
    mutex_unlock(&control.lock);
    return requested;
}

int signals_take_notes(char notes[][SIGNAL_NOTE_SIZE], int max) {
    mutex_lock(&control.lock);
    int count = control.note_count < max ? control.note_count : max;
    for (int i = 0; i < count; i++) {
        memcpy(notes[i], control.notes[i], SIGNAL_NOTE_SIZE);
    }
    control.note_count = 0;
    mutex_unlock(&control.lock);
    return count;
}

int signals_received(void) {
    mutex_lock(&control.lock);
    int received = control.received;
    mutex_unlock(&control.lock);
    return received;
}
