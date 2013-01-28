/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */

#ifndef __esh_h__
#define __esh_h__

extern int interactive;
extern int exception_flag;
extern int stderr_handler_fd;

extern hash_table* aliases;
extern hash_table* defines;
extern hash_table* builtins;
extern list* jobs;
extern list* prompt;
extern list* stack;
extern list* ls_true;
extern list* ls_false;
extern list* ls_stdio;
extern list* ls_stderr;
extern list* ls_void;
extern char** environ;

extern char* syntax_blank;

extern char* dynamic_strcpy(char* chr);

extern char* file_read(int fd);
extern void file_write(int fd, char* data);

extern char next_token(char* input, int* i, char** value, int* len);
extern list* parse_builtin(char* input, int* len, int liter, int delay);
extern list* parse_split(char* input);

extern pid_t do_pipe(int f_src, int f_out, list* ls, int bg, int destruc);
extern list* do_builtin(list* ls);
extern void do_file(char* file, int do_error);

extern void ls_print(list* ls);
extern char* ls_strcat(list* ls);

extern void job_foreground(job_t* job);
extern void job_background(job_t* job);

#endif /* !__esh_h__ */
