/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */

#ifndef __format_h__
#define __format_h__

extern void signoff(const char* fmt, ...);
extern void error(const char* fmt, ...);
extern void error_simple(const char* fmt, ...);

#endif /* !__format_h__ */
