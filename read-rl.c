/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#include <stdlib.h>
#include <stdio.h>

#include <readline.h>
#include <history.h>

#include "common.h"
#include "gc.h"
#include "list.h"
#include "hash.h"
#include "read.h"


static int rl_literal_newline(int count, int key) {
  rl_insert_text("\n");

  return 0;
}

static char* dynamic_strcpy_malloc(char* str) {
  char* tmp = (char*)malloc(sizeof(char) * (strlen(str) + 1));
  strcpy(tmp, str);

  return tmp;
}

static char* rl_find_builtin(char* word, int state) {
  static int len = 0;
  static list* hash_ls1 = NULL;
  static list* hash_ls2 = NULL;

  static int which_hash = 0;
  static list* iter = NULL;


  if (state == -1) {
    ls_free_all(hash_ls1);
    ls_free_all(hash_ls2);

    return NULL;

  } else if (state == 0) {
    len = strlen(word);

    ls_free_all(hash_ls1);
    ls_free_all(hash_ls2);

    hash_ls1 = hash_keys(builtins);
    hash_ls2 = hash_keys(defines);

    which_hash = 0;
    iter = hash_ls1;
  }

  while (1) {
    if (!iter) {
      if (!which_hash) {
	which_hash = 1;
	iter = hash_ls2;

      } else {
	return NULL;
      }
    }

    if (strncmp(ls_data(iter), word, len) == 0) {
      char* ret = dynamic_strcpy_malloc(ls_data(iter));

      iter = ls_next(iter);

      return ret;
    }

    iter = ls_next(iter);

  }
}

static char** rl_esh_completion(char* word, int start, int end) {

  /* If the first non-whitespace character before the word is an
   * open parentheses, then complete a command. Otherwise, fallback to
   * the default completer. */

  if (start) {
    start--;

    while (start && blank(rl_line_buffer[start])) {
      start--;
    }
  }

  if (openparen(rl_line_buffer[start])) {
    return completion_matches(word, rl_find_builtin);

  } else {
    return NULL;
  }
}

void read_init(void) {
  rl_bind_key('\012', rl_literal_newline);

  /* rl_catch_signals = 0; */
  rl_attempted_completion_function = rl_esh_completion;
}

char* read_read(char* prompt) {
  char* line = readline(prompt);
  char* ret;

  if (!line) return NULL;

  if (*line) {
    add_history(line);
  }

  ret = dynamic_strcpy(line);

  free(line);

  return ret;
}

void read_done(void) {

  /* Free the completion buffers. */
  rl_find_builtin("", -1);
}
