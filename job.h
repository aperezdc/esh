/* 
 * esh, the Unix shell with Lisp-like syntax. 
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#ifndef JOB_H
#define JOB_H

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


#endif

