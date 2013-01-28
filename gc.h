/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */

#ifndef _GC_H
#define _GC_H


typedef struct gc_node gc_node;

struct gc_node {
  void* data;
  char* where;
  gc_node* next;
};


extern void* gc_alloc(size_t size, char* where);
extern void gc_inc_ref(void* ptr);
extern void gc_add_ref(void* ptr, int add);
extern int gc_refs(void* ptr);
extern void gc_free(void* ptr);
extern void gc_diagnostics(void);

#endif

