/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */

#ifndef __list_h__
#define __list_h__

/*
 * A very simple linked-list implementation.
 *
 * Pitfalls:
 *
 *  + You cannot delete elements from the list.
 *  + The list functions are lisp-like, i.e. "ls_cons" returns a list.
 *  + Please don't use the internals of the "list" structure.
 *  + You can accidentaly reverse the arguments to "ls_cons"
 *    and not get a warning from the compiler!
 *  + The list does not do any memory management. Nothing is copied before
 *    insertion, and "ls_free" does not free the data in the list.
 *    "ls_free_all" is provided as a convinience -- it will free
 *    the data before deleting the list node.
 *  + "ls_copy" and "ls_free_all" make lots of assumptions about type
 *     information.
 */

#define TYPE_STRING   0
#define TYPE_LIST     1
#define TYPE_HASH     2
#define TYPE_BOOL     3
#define TYPE_FD       4
#define TYPE_PROC     5
#define TYPE_VOID     6

#define FLAG_NONE     0

typedef struct list list;

struct list {
  void* data;
  list* next;
  char type;
  char flag;
};

extern void ls_free(list* ls);
extern void ls_free_all(list* ls);
extern void ls_free_shallow(list* ls);
extern list* ls_reverse(list* ls);
extern list* ls_cons(void* data, list* ls);
extern list* ls_next(list* ls);
extern void* ls_data(list* ls);
extern void ls_type_set(list* ls, char type);
extern char ls_type(list* ls);
extern void ls_flag_set(list* ls, char flag);
extern char ls_flag(list* ls);
extern list* ls_copy(list* ls);

#endif /* !__list_h__ */
