/* 
 * esh, the Unix shell with Lisp-like syntax. 
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#include <stdlib.h>
#include <stdio.h>

#include "gc.h"
#include "format.h"

/*
 * A simplistic garbage collection mechanism. It requires that the C
 * program call gc_free whenever an object should be deleted. As such,
 * this cannot really be called garbage collection, though it does serve
 * a purpose as a central repository of all allocated memory.
 */


int __gc_alloc = 0;


void* gc_alloc(size_t size, char* where) {
  void* ret = malloc(size + sizeof(int));
  
  if (!ret) {
    error("esh: could not allocate memory.");
    exit(EXIT_FAILURE);
  }
  
  ((int*)ret)[0] = 1;

  __gc_alloc++;

  return ret + sizeof(int);
}


inline void gc_inc_ref(void* ptr) {
  int* ref = (int*)(ptr - sizeof(int));

  if ((*ref) <= 0) {
    error("esh: refcount is corrupted in gc_inc_ref.");
    exit(EXIT_FAILURE);
  }

  (*ref)++;
  __gc_alloc++;
}

void gc_add_ref(void* ptr, int add) {
  int* ref = (int*)(ptr - sizeof(int));

  if ((*ref) <= 0) {
    error("esh: refcount is corrupted in gc_add_ref");
    exit(EXIT_FAILURE);
  }

  (*ref) += add;
  __gc_alloc += add;

  if ((*ref) <= 0) {
    error("esh: tried to set an invalid ref count.");
    exit(EXIT_FAILURE);
  }
}


inline int gc_refs(void* ptr) {
  int* ref = (int*)(ptr - sizeof(int));

  if ((*ref) <= 0) {
    error("esh: refcount is corrupted in gc_refs.");
    exit(EXIT_FAILURE);
  }

  return (*ref);
}


inline void gc_free(void* ptr) {
  int* ref = (int*)(ptr - sizeof(int));

  if ((*ref) <= 0) {
    error("esh: refcount is corrupted in gc_free.");
    exit(EXIT_FAILURE);
  }

  (*ref)--;
  __gc_alloc--;

  if (!(*ref)) {
    free(ptr - sizeof(int));
  }
}


void gc_diagnostics(void) {
  printf("\nAllocated chunks: %d\n", __gc_alloc);
}


