/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */

#ifndef __job_h__
#define __job_h__

#include <termios.h>

#define JOB_RUNNING    0
#define JOB_STOPPED    1
#define JOB_DEAD       2

typedef struct job_t job_t;

struct job_t {
  pid_t pgid;
  pid_t last_pid;
  char* name;

  char status;
  char value;
  struct termios terminal_modes;
};

#endif /* !__job_h__ */
