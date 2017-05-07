#ifndef _SYS_SIGNAL_H_
#define _SYS_SIGNAL_H_

#include <unistd.h>

/* TODO: Should we somehow merge this file with kernel's signal.h? */

typedef enum {
  SIGINT = 1,
  SIGILL,
  SIGABRT,
  SIGFPE,
  SIGSEGV,
  SIGKILL,
  SIGTERM,
  SIGCHLD,
  SIGUSR1,
  SIGUSR2,

  SIG_LAST
} signo_t;

typedef void (*sighandler_t)(int);

#define SIG_DFL 0x00
#define SIG_IGN 0x01

typedef struct sigaction {
  sighandler_t sa_handler;
  void *sa_restorer;
} sigaction_t;

int sigaction(int signum, const sigaction_t *act, sigaction_t *oldact);
int kill(int tid, int sig);

static inline int raise(int sig) {
  return kill(getpid(), sig);
}

void sigreturn();
static inline int my_signal(int sig, sighandler_t handler) {
  sigaction_t sa = {.sa_handler = handler, .sa_restorer = sigreturn};
  return sigaction(sig, &sa, NULL);
}

/* This is necessary to keep newlib happy. */
typedef sighandler_t _sig_func_ptr;

#endif /* _SYS_SIGNAL_H_ */