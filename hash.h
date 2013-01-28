/* 
 * esh, the Unix shell with Lisp-like syntax. 
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


/*
 * A very simple hash table implementation, using "buckets".
 *
 * Pitfalls: 
 *
 *  + You cannot delete elements from the cache.
 *  + Calling "hash_put" or "hash_get" before "hash_init" likely
 *    means a segfault.
 *  + "hash_init" should always be called. Pass a NULL as the second
 *    argument if you don't want any initial data.
 *  + Calling"hash_init" more than once on the same hash table
 *    constitutes a giant memory leak!
 *  + The argument to "hash_init" is terminated with a { NULL, NULL }
 *  + The hash table does not do any memory management -- i.e.
 *    the arguments to "hash_put" are not copied before they are inserted 
 *    into the hash table.
 *  + However, there is an ugly loophole in the code around the above
 *    pitfall, but only if the hash data is lists!
 *  + "hash_put" returns the previous data with the same key, if any.
 *    It is your responsibility to free this data, if necessary.
 *  + "hash_inc_ref"  and "hash_put_inc_ref" assume that the hash table 
 *    holds only lists.
 */

#ifndef HASH_H
#define HASH_H

typedef struct hash_entry hash_entry;
typedef list** hash_table;

struct hash_entry {
  char* key;
  void* data;
};

extern void* hash_put(hash_table* t, char* key, void* data);
extern void* hash_put_inc_ref(hash_table* t, char* key, void* data);
extern void hash_init(hash_table* t, hash_entry data[]);
extern void* hash_get(hash_table* t, char* key);

extern void hash_free(hash_table* t, 
		      void (*func)());

extern void hash_inc_ref(hash_table* t);
extern list* hash_keys(hash_table* t);

#endif

