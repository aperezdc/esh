/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */

#ifndef __builtins_h__
#define __builtins_h__

extern hash_entry builtins_array[];

extern list* eval(list* arg);
extern void register_chdir(void);

#endif /* !__builtins_h__ */
