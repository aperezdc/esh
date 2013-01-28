/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>

#include "common.h"
#include "gc.h"
#include "list.h"
#include "hash.h"
#include "read.h"


void read_init(void) {
  /* Nothing. */
}

char* read_read(char* prompt) {
  static int interactive = -1;

  int len = 80;
  int i = 0;
  char* ret = (char*)gc_alloc(sizeof(char) * len, "read_read");
  char foo = 0;

  if (interactive < 0) {
    interactive = isatty(STDIN_FILENO);
  }

  if (interactive) {
    printf(prompt);
    fflush(stdout);
  }

  while (foo != '\n') {
    if (read(STDIN_FILENO, &foo, 1) <= 0) {
      gc_free(ret);
      return NULL;
    }

    if (i >= len-2) {
      char* tmp = (char*)gc_alloc(sizeof(char) * len * 2, "read_read");

      len *= 2;

      strcpy(tmp, ret);
      gc_free(ret);
      ret = tmp;
    }

    ret[i] = foo;
    i++;
  }

  ret[i] = '\0';

  return ret;

}

void read_done(void) {
  /* Nothing. */
}
