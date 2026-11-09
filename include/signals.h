#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdbool.h>
#include <sys/types.h>

// Координатор: SIGINT (Ctrl+C), SIGTERM и SIGHUP только запоминаются, игра
// доигрывает текущий такт, завершает участников и удаляет семафоры и память.
// Обработчик ставится без SA_RESTART, чтобы сигнал прерывал ожидание семафора.
void signals_setup_coordinator(void);
// номер полученного сигнала остановки или 0
int signals_stop_signal(void);

// дочерний процесс (fork): Ctrl+C приходит всей группе процессов, но решает
// координатор, поэтому SIGINT и SIGHUP игнорируются
void signals_setup_fork_child(void);

// независимо запущенный участник: пока координатор не известен, сигнал
// остановки просто запоминается; потом пересылается координатору
void signals_setup_participant(void);
void signals_forward_to(pid_t coordinator);
bool signals_participant_stop(void);

#endif
