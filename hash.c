/* 
 * esh, the Unix shell with Lisp-like syntax. 
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */



#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "list.h"
#include "hash.h"

#define HASH_SIZE 1024


/*
 * A simple but braindead hasher function.
 */
static int hash(char* key) {
  int i = 0, j = 0;

  if (!key) return 0;

  while (key[j]) {
    i += key[j++];
  }

  return i % HASH_SIZE;
}


/*
 * Warning: If "doinc" is true, this function assumes that the hash data is
 * lists!
 */

static void* hash_put_aux(hash_table* _hash_array, char* key, void* data,
			  int doinc) {
  hash_entry* hs_ent;
  void* ret;

  int idx = hash(key);

  list* bucket = (*_hash_array)[idx];
  list* iter;

  int refs = gc_refs((*_hash_array));
  int i;

  for (iter = bucket; iter != NULL; iter = ls_next(iter)) {
    if (strcmp(((hash_entry*)ls_data(iter))->key, key) == 0) {

      hs_ent = (hash_entry*)ls_data(iter);

      ret = hs_ent->data;

      hs_ent->data = data;

      if (doinc) {
	for (i = 1; i < refs; i++) {
	  hs_ent->data = ls_copy(hs_ent->data);
	  ls_free_all(ret);
	}
      }

      return ret;
    }
  }

  hs_ent = (hash_entry*)gc_alloc(sizeof(hash_entry), "hash_put");

  hs_ent->key = key;
  hs_ent->data = data;
  
  (*_hash_array)[idx] = ls_cons(hs_ent, bucket);

  if (doinc) {
    gc_add_ref((*_hash_array)[idx], refs-1);
    gc_add_ref(hs_ent, refs-1);

    gc_add_ref(hs_ent->key, refs-1);

    for (i = 1; i < refs; i++) {
      hs_ent->data = ls_copy(hs_ent->data);
    }
  }

  return NULL;
}


inline void* hash_put(hash_table* tab, char* key, void* data) {
  return hash_put_aux(tab, key, data, 0);
}

inline void* hash_put_inc_ref(hash_table* tab, char* key, void* data) {
  return hash_put_aux(tab, key, data, 1);
}


void* hash_get(hash_table* _hash_array, char* key) {
  int idx = hash(key);

  list* bucket = (*_hash_array)[idx];
  list* iter;

  for (iter = bucket; iter != NULL; iter = ls_next(iter)) {
    if (strcmp(((hash_entry*)ls_data(iter))->key, key) == 0) 
      return ((hash_entry*)ls_data(iter))->data;
  }

  return NULL;
}

/*
 * Uglification alert.
 */
static char* dynamic_strcpy(char* str) {
  char* tmp = (char*)gc_alloc(sizeof(char) * (strlen(str) + 1), 
                              "hash.c:dynamic_strcpy");

  strcpy(tmp, str);

  return tmp;
}


void hash_init(hash_table* _hash_array, hash_entry data[]) {
  int i;

  (*_hash_array) = (list**)gc_alloc(sizeof(list*) * HASH_SIZE,
				    "hash_init");

  for (i = 0; i < HASH_SIZE; i++)
    (*_hash_array)[i] = NULL;

  i = 0;

  if (!data) return;

  while (data[i].key != NULL) {
    hash_put(_hash_array, dynamic_strcpy(data[i].key), data[i].data);
    i++;
  }
}

void hash_free(hash_table* tab, 
	       void (*func)()) {
  int i;
  list* iter;

  for (i = 0; i < HASH_SIZE; i++) {
    for (iter = (*tab)[i]; iter != NULL; iter = ls_next(iter)) {
      hash_entry* he = (hash_entry*)(ls_data(iter));

      gc_free(he->key);

      if (func)
	func(he->data);

      gc_free(he);
    }

    ls_free((*tab)[i]);
    
  }

  gc_free((*tab));
}


/*
 * Warning: This function assumes that hash values are lists!
 */

void hash_inc_ref(hash_table* tab) {
  int i;
  list* iter;

  for (i = 0; i < HASH_SIZE; i++) {
    for (iter = (*tab)[i]; iter != NULL; iter = ls_next(iter)) {
      hash_entry* he = (hash_entry*)(ls_data(iter));

      gc_inc_ref(iter);
      gc_inc_ref(he);

      gc_inc_ref(he->key);
      he->data = ls_copy(he->data);
    }
  }

  gc_inc_ref((*tab));
}



list* hash_keys(hash_table* tab) {
  int i;
  list* iter;

  list* ret = NULL;

  for (i = 0; i < HASH_SIZE; i++) {
    for (iter = (*tab)[i]; iter != NULL; iter = ls_next(iter)) {
      hash_entry* he = (hash_entry*)(ls_data(iter));

      gc_inc_ref(he->key);

      ret = ls_cons(he->key, ret);
    }
  }

  return ret;
}
