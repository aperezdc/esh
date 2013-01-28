/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */

#ifndef FORMAT_H
#define FORMAT_H

extern void signoff(const char* fmt, ...);
extern void error(const char* fmt, ...);
extern void error_simple(const char* fmt, ...);

#endif

