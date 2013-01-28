/* 
 * esh, the Unix shell with Lisp-like syntax. 
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#include <stdlib.h>
#include <unistd.h>

#include "gc.h"
#include "list.h"
#include "hash.h"

extern int stderr_handler_fd;

list* ls_cons(void* data, list* ls) {
  list* nw = (list*)gc_alloc(sizeof(list), "ls_cons");

  nw->data = data;
  nw->next = ls;
  nw->type = TYPE_STRING;
  nw->flag = FLAG_NONE;
  return nw;
}

void ls_type_set(list* ls, char type) {
  ls->type = type;
}

inline char ls_type(list* ls) {
  return ls->type;
}

void ls_flag_set(list* ls, char flag) {
  ls->flag = flag;
}

inline char ls_flag(list* ls) {
  return ls->flag;
}

void ls_free(list* ls) {
  if (!ls) return;

  if (ls->next) {
    ls_free(ls->next);
  }

  if (ls->type == TYPE_LIST) {
    ls_free(ls->data);
  }

  gc_free(ls);
}

void ls_free_all(list* ls) {
  if (!ls) return;

  if (ls->next) {
    ls_free_all(ls->next);
  }

  switch (ls->type) {
  case TYPE_LIST:
    ls_free_all(ls->data);
    break;

  case TYPE_STRING:
  case TYPE_PROC:
    if (ls->data)
      gc_free(ls->data);
    break;

  case TYPE_FD:
    if (gc_refs(ls->data) == 1) {
      int* fd = ls->data;

      if (fd[0] != STDIN_FILENO &&
	  fd[0] != stderr_handler_fd) {

	close(fd[0]);
      }

      if (fd[1] != STDOUT_FILENO &&
	  fd[1] != STDERR_FILENO &&
	  fd[1] != fd[0] &&
	  fd[1] != stderr_handler_fd) {

	close(fd[1]);
      }
    }

    gc_free(ls->data);
    break;

  case TYPE_HASH:
    hash_free(ls->data, ls_free_all);
    gc_free(ls->data);
    break;

  case TYPE_VOID:
  case TYPE_BOOL:
    break;
  }

  gc_free(ls);
}

void ls_free_shallow(list* ls) {
  if (!ls) return;
  
  if (ls->next) {
    ls_free_shallow(ls->next);
  }

  gc_free(ls);
}

list* ls_reverse(list* ls) {
  list* ret = NULL;
  list* i = ls;

  while (i) {
    ret = ls_cons(i->data, ret);
    ret->type = i->type;
    ret->flag = i->flag;
    i = i->next;
  }

  ls_free_shallow(ls);

  return ret;
}

inline list* ls_next(list* ls) {
  return ls->next;
}

inline void* ls_data(list* ls) {
  return ls->data;
}


list* ls_copy(list* arg) {
  list* ret = arg;

  for (; arg != NULL; arg = ls_next(arg)) {

    gc_inc_ref(arg);

    switch (ls_type(arg)) {
    case TYPE_LIST:
      ls_copy(ls_data(arg));
      break;

    case TYPE_STRING:
    case TYPE_FD:
    case TYPE_PROC:
      gc_inc_ref(ls_data(arg));
      break;

    case TYPE_HASH:
      hash_inc_ref(ls_data(arg));
      gc_inc_ref(ls_data(arg));
      break;

    case TYPE_VOID:
    case TYPE_BOOL:
      break;
    }
  }
  
  return ret;
}


