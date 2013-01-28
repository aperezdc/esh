/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


extern hash_table* defines;
extern hash_table* builtins;

extern int blank(char);
extern int openparen(char);
extern int closeparen(char);
extern int separator(char);
extern int redirect_in(char);
extern int redirect_out(char);
extern int quote(char);
extern int literal(char);
extern int delaysym(char);
extern int comment(char);
extern int special(char);

extern char* dynamic_strcpy(char*);

extern void read_init(void);
extern char* read_read(char* prompt);
extern void read_done(void);

