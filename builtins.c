/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <regex.h>

#include "common.h"
#include "format.h"
#include "list.h"
#include "gc.h"
#include "hash.h"
#include "job.h"
#include "esh.h"
#include "builtins.h"
#include "read.h"



static int typecheck_aux(char* tspec, list* data, int* i, int quiet) {
  int len = strlen(tspec);
  int err = 0;
  int stoploop = 0;

  for (; (*i) <= len; (*i)++) {

    if (stoploop || err) break;

    if (!data) {
      if (tspec[(*i)] && tspec[(*i)] != ')') {
	err = 3;
      }

      break;
    }

    switch (tspec[(*i)]) {
    case 's':
    case 'l':
    case 'h':
    case 'b':
    case 'f':
    case 'p':
      {
	int type = TYPE_STRING;

	switch (tspec[(*i)]) {
	case 's':     type = TYPE_STRING; break;
	case 'l':     type = TYPE_LIST;   break;
	case 'h':     type = TYPE_HASH;   break;
	case 'b':     type = TYPE_BOOL;   break;
	case 'f':     type = TYPE_FD;     break;
	case 'p':     type = TYPE_PROC;   break;
	}

	if (ls_type(data) != type) err = 1;

	data = ls_next(data);
	break;
      }

    case '?':
      data = ls_next(data);
      break;

    case ')':
    case '\0':
      err = 2;
      stoploop = 1;
      break;

    case '*':
      while (data) {
	data = ls_next(data);
      }

      break;

    case '(':
      if (ls_type(data) != TYPE_LIST) {
	err = 1;

      } else {
	(*i)++;
	err = typecheck_aux(tspec, ls_data(data), i, quiet);
      }

      data = ls_next(data);
      break;

    case 'S':
    case 'L':
    case 'H':
    case 'B':
    case 'F':
    case 'P':
      {
	int type = TYPE_STRING;

	switch (tspec[(*i)]) {
	case 'S':     type = TYPE_STRING; break;
	case 'L':     type = TYPE_LIST;   break;
	case 'H':     type = TYPE_HASH;   break;
	case 'B':     type = TYPE_BOOL;   break;
	case 'F':     type = TYPE_FD;     break;
	case 'P':     type = TYPE_PROC;   break;
	}

	if (ls_type(data) != type) {
	  err = 1;
	  break;
	}

	while (data && ls_type(data) == type) {
	  data = ls_next(data);
	}

	break;
      }

    default:
      err = 4;
      stoploop = 1;
      break;

    }
  }

  if (data && !err) err = 2;

  if (!quiet) {
    switch (err) {
    case 1:
      error("esh: type of the given arguments did not match "
	    "the required type.");
      break;

    case 2:
      error("esh: extraneous arguments given.");
      break;

    case 3:
      error("esh: not enough arguments given.");
      break;

    case 4:
      error("esh: format for type specification string is incorrect.");
      break;
    }
  }

  return err;
}


static inline int typecheck(char* tspec, list* arg) {
  int i = 0;

  return typecheck_aux(tspec, arg, &i, 0);
}


static inline int quiet_typecheck(char* tspec, list* arg) {
  int i = 0;

  return typecheck_aux(tspec, arg, &i, 1);
}


static int fancy_typecheck(char* tspec, list* arg,
			   char* cmdname, char* cmddesc) {

  int ret = typecheck(tspec, arg);

  if (ret) {
    int len = strlen(tspec);
    int i;

    printf("\nUsage: (%s", cmdname);

    for (i = 0; i < len; i++) {
      printf(" ");

      switch (tspec[i]) {
      case 's':
	printf("<string>");
	break;

      case 'l':
	printf("<list>");
	break;

      case 'h':
	printf("<hash table>");
	break;

      case 'b':
	printf("<bool>");
	break;

      case 'f':
	printf("<file>");
	break;

      case 'p':
	printf("<process>");
	break;

      case '?':
	printf("<any>");
	break;

      case 'S':
	printf("<string>...");
	break;

      case 'L':
	printf("<list>...");
	break;

      case 'H':
	printf("<hash table>...");
	break;

      case 'B':
	printf("<bool>...");
	break;

      case 'F':
	printf("<file>...");
	break;

      case 'P':
	printf("<process>...");
	break;

      case '*':
	printf("...");
	break;

      case '(':
	printf("(");
	break;

      case ')':
	printf(")");
	break;
      }
    }

    printf(")\n%s\n\n", cmddesc);
  }

  return ret;
}


static long do_atoi(char* dat, int* err, long def) {
  char* merr;
  long ret;

  if (!dat) return def;

  ret = strtol(dat, &merr, 0);
  *err = 0;

  if (*dat == '\0' || *merr != '\0') {
    *err = 1;
    return def;
  }

  return ret;
}

list* car(list* arg) {
  list* ret = NULL;
  list* foo;

  if (!arg) return NULL;

  switch (ls_type(arg)) {
  case TYPE_LIST:
    foo = ls_copy(ls_data(arg));
    ret = ls_cons(foo, NULL);
    break;

  case TYPE_STRING:
  case TYPE_FD:
  case TYPE_PROC:
    gc_inc_ref(ls_data(arg));
    ret = ls_cons(ls_data(arg), NULL);
    break;

  case TYPE_HASH:
    hash_inc_ref(ls_data(arg));
    gc_inc_ref(ls_data(arg));

    ret = ls_cons(ls_data(arg), NULL);
    break;

  case TYPE_BOOL:
    ret = ls_cons(ls_data(arg), NULL);
    break;
  }

  ls_type_set(ret, ls_type(arg));
  ls_flag_set(ret, ls_flag(arg));

  return ret;
}



void register_chdir(void) {
  char* buff1;
  char* buff2;
  char* foo = getenv("PWD");
  char bar[256];

  if (!getcwd(bar, 256)) {
    error("esh: cd: cannot read current directory.");
    return;
  }

  /* Hack -- this will memory leak slightly, but will work on
   * systems that expect a mallocated string in "putenv". */

  buff2 = (char*)malloc(sizeof(char) * (strlen(bar) + 6));

  sprintf(buff2, "PWD=%s", bar);

  if (foo) {
    buff1 = (char*)malloc(sizeof(char) * (strlen(foo) + 9));

    sprintf(buff1, "OLDPWD=%s", foo);
    putenv(buff1);
  }

  putenv(buff2);
}


static list* cd(list* arg) {
  char* dirnm;

  if (arg && ls_data(arg) &&
      fancy_typecheck("s", arg, "cd",
		      "This command changes the current directory.")) {
    return NULL;
  }

  if (!arg) {
    dirnm = getenv("HOME");
  } else {
    dirnm = ls_data(arg);
  }

  if (!dirnm) {
    dirnm = "/";
  }

  if (dirnm[0] == '-' && dirnm[1] == '\0') {
    char* ge = getenv("OLDPWD");

    if (ge) {
      dirnm = ge;

    } else {
      error("esh: there is no previous directory.");
      return NULL;
    }
  }

  if (chdir(dirnm) < 0) {
    error("esh: cd: could not change to directory \"%s\".", dirnm);

  } else {
    register_chdir();
  }

  return NULL;
}



static list* help(list* arg) {
  int i = 0;

  printf("esh version %d.%d.%d, builtin command list.\n"
	 "To get help on an individual command, try running the command\n"
	 "without any arguments.\n\n", VERSION_MAJOR, VERSION_MINOR,
	 VERSION_PATCH);

  while (builtins_array[i].key) {
    printf("%s\n", builtins_array[i].key);
    i++;
  }

  return NULL;
}


list* eval_aux(list* arg, int mode, int strength) {
  list* iter;
  list* ret = NULL;
  list* tmp;
  list* tmp2;

  for (iter = arg; iter != NULL; iter = ls_next(iter)) {

    switch (ls_type(iter)) {
    case TYPE_LIST:
      {
	list* rec = ls_data(iter);

	if (!mode && strength < ls_flag(iter)) {
	  strength = ls_flag(iter);
	}

	if (strength < ls_flag(iter)) {

	  ret = ls_cons(ls_copy(rec), ret);
	  ls_type_set(ret, TYPE_LIST);
	  ls_flag_set(ret, ls_flag(iter));

	} else {

	  tmp = eval_aux(rec, 1, strength);

	  if (tmp) {
	    for (tmp2 = tmp; tmp2 != NULL; tmp2 = ls_next(tmp2)) {

	      if (ls_type(tmp2) == TYPE_VOID) continue;

	      ret = ls_cons(ls_data(tmp2), ret);
	      ls_type_set(ret, ls_type(tmp2));
	      ls_flag_set(ret, ls_flag(tmp2));
	    }

	    ls_free_shallow(tmp);

	  } else {
	    ret = ls_cons(NULL, ret);
	    ls_type_set(ret, TYPE_LIST);
	  }
	}
      }

      break;

    case TYPE_STRING:
    case TYPE_FD:
    case TYPE_PROC:
      gc_inc_ref(ls_data(iter));
      ret = ls_cons(ls_data(iter), ret);

      ls_type_set(ret, ls_type(iter));
      ls_flag_set(ret, ls_flag(iter));
      break;

    case TYPE_HASH:
      hash_inc_ref(ls_data(iter));
      gc_inc_ref(ls_data(iter));

      ret = ls_cons(ls_data(iter), ret);

      ls_type_set(ret, TYPE_HASH);
      ls_flag_set(ret, ls_flag(iter));
      break;

    case TYPE_BOOL:
      ret = ls_cons(ls_data(iter), ret);

      ls_type_set(ret, TYPE_BOOL);
      ls_flag_set(ret, ls_flag(iter));
      break;
    }
  }

  ret = ls_reverse(ret);

  if (mode) {
    tmp = do_builtin(ret);

    ls_free_all(ret);

    return tmp;

  } else {
    return ret;
  }
}


inline list* eval(list* arg) {
  list* ret;

  ret = eval_aux(arg, 0, 0);

  return ret;
}


static list* set(list* arg) {
  int len;
  char* str;

  char* key;
  char* val;

  if (fancy_typecheck("ss", arg, "set",
		      "This command manipulates the environment.")) {
    return NULL;
  }

  key = ls_data(arg);
  val = ls_data(ls_next(arg));

  len = strlen(key) + strlen(val) + 3;
  str = (char*)malloc(sizeof(char) * len);

  sprintf(str, "%s=%s", key, val);

  putenv(str);

  return NULL;
}

static list* get(list* arg) {
  char* tmp;
  char* key;

  if (fancy_typecheck("s", arg, "get",
		      "This command examines the environment.")) {
    return NULL;
  }

  key = ls_data(arg);

  tmp = getenv(key);

  if (!tmp) {
    return NULL;
  } else {
    return ls_cons(dynamic_strcpy(tmp), NULL);
  }
}


static list* env(list* arg) {
  int i;

  if (fancy_typecheck("", arg, "env",
		      "This command prints the environment "
		      "on the standard output.")) {
    return NULL;
  }

  for (i = 0; environ[i]; i++) {
    printf("%s\n", environ[i]);
  }

  return NULL;
}

static list* run(list* arg) {
  int* fd1;
  int* fd2;
  int ret;

  if (fancy_typecheck("bffL", arg, "run",
		      "This command runs the specified executables, "
		      "with a pipeline in between\neach command.\n"
		      "The first argument specifies whether the command "
		      "should be run\nin the background or not.\n"
		      "The next two arguments are the input and output "
		      "redirection files,\nrespectively. "
		      "You can pass \"(standard\") "
		      "to use the standard input/output.\n"
		      "This command is equivalent to typing "
		      "\"cmd1, cmd2, ...\" "
		      "while running the shell\ninteractively.\n"
		      "This command returns the PID of the pipeline, "
		      "if the command\nis in the background.")) {
    return NULL;
  }

  fd1 = ls_data(ls_next(arg));
  fd2 = ls_data(ls_next(ls_next(arg)));

  ret = do_pipe(fd1[0], fd2[1], ls_next(ls_next(ls_next(arg))),
		(int)ls_data(arg), 0);

  if ((int)ls_data(arg)) {
    if (ret > 0) {
      pid_t* foo;
      list* bar;

      foo = (pid_t*)gc_alloc(sizeof(pid_t), "run");
      (*foo) = ret;
      bar = ls_cons(foo, NULL);
      ls_type_set(bar, TYPE_PROC);

      return bar;
    }

  } else {
    char* decchar = (char*)gc_alloc(sizeof(char) * 5, "run");

    sprintf(decchar, "%d", ret);

    return ls_cons(decchar, NULL);
  }

  return NULL;
}

static list* run_simple(list* arg) {
  int ret;
  char* decchar;

  if (fancy_typecheck("L", arg, "run-simple",
		      "This command is equivalent to "
		      "\"(run (false) (standard) (standard) ...)\"")) {
    return NULL;
  }

  ret = do_pipe(STDIN_FILENO, STDOUT_FILENO, arg, 0, 0);

  decchar = (char*)gc_alloc(sizeof(char) * 5, "run");

  sprintf(decchar, "%d", ret);

  return ls_cons(decchar, NULL);
}

static list* gobble(list* arg) {
  int pfd[2];
  int* fd;
  list* ret;
  pid_t foo;

  if (fancy_typecheck("fL", arg, "gobble",
		      "This command is equivalent to \"run\", except "
		      "that it will\nreturn the output of the pipeline, "
		      "as a string.\n"
		      "The first argument specifies the "
		      "pipe's input file.")) {
    return NULL;
  }

  if (pipe(pfd)) {
    error("esh: gobble: could not create a pipe.");
    return NULL;
  }


  fd = ls_data(arg);

  fcntl(pfd[0], F_SETFD, 1);
  fcntl(pfd[1], F_SETFD, 1);

  foo = do_pipe(fd[0], pfd[1], ls_next(arg), 1, 1);

  if (foo < 0) return NULL;

  ret = ls_cons(file_read(pfd[0]), NULL);

  close(pfd[0]);
  close(pfd[1]);

  return ret;
}


static list* my_exit(list* arg) {
  int stat = EXIT_SUCCESS;
  int err = 0;
  char* tmp = "0";

  if (arg && ls_data(arg) &&
      fancy_typecheck("s", arg, "exit",
		      "This command exits with the given exit status.")) {
    return NULL;
  }

  if (arg && ls_data(arg)) {
    tmp = ls_data(arg);
  }

  if (tmp) {
    stat = do_atoi(tmp, &err, stat);

    if (err) {
      error("esh: exit: \"exit\" takes a numeric exit status.");
      return NULL;
    }
  }

  exit(stat);
}


static list* alias(list* arg) {
  list* copy = NULL;

  list* old = NULL;

  if (quiet_typecheck("sL", arg) &&
      fancy_typecheck("sS", arg, "alias",
		      "This command will create an alias with the given name "
		      "and expansion.\nNote that the arguments have to be "
		      "defined as a list, not as a string.\n"
		      "i.e. (alias ls ls -l) not (alias ls 'ls -l').\n\n"
		      "(alias <string> <list>...) will instead evaluate the "
		      "given\nlists as commands whenever the alias is run.\n"
		      "i.e. (alias cd ~(cd (top)) to mimic the traditional "
		      "syntax\nof \"cd\".")) {
    return NULL;
  }

  copy = ls_copy(arg);

  old = hash_put_inc_ref(aliases, ls_data(copy), ls_next(copy));

  if (old) {
    gc_free(ls_data(copy));
    ls_free_all(old);
  }

  gc_free(copy);

  return NULL;
}


static list* plus(list* arg) {
  int tot = 0;
  int err;
  char* ret = (char*)gc_alloc(sizeof(char) * 80, "plus");
  char* tmp;

  if (fancy_typecheck("S", arg, "+", "This command adds its arguments.")) {
    gc_free(ret);
    return NULL;
  }

  for (; arg != NULL; arg = ls_next(arg)) {
    tmp = ls_data(arg);

    if (!tmp) continue;

    tot += do_atoi(tmp, &err, 0);

    if (err) {
      error("esh: +: \"+\" only accepts numeric arguments.");
      gc_free(ret);
      return NULL;
    }
  }

  sprintf(ret, "%d", tot);

  return ls_cons(ret, NULL);
}




static list* times(list* arg) {
  int tot = 1;
  int err;
  char* ret = (char*)gc_alloc(sizeof(char) * 80, "times");
  char* tmp;

  if (fancy_typecheck("S", arg, "*", "This command multiplies its "
		      "arguments.")) {
    gc_free(ret);
    return NULL;
  }

  for (; arg != NULL; arg = ls_next(arg)) {
    tmp = ls_data(arg);

    if (!tmp) continue;

    tot *= do_atoi(tmp, &err, 1);

    if (err) {
      error("esh: *: \"*\" only accepts numeric arguments.");
      gc_free(ret);
      return NULL;
    }
  }

  sprintf(ret, "%d", tot);

  return ls_cons(ret, NULL);
}

static list* minus(list* arg) {
  int tot = 0;
  int err;
  char* ret = (char*)gc_alloc(sizeof(char) * 80, "minus");
  char* tmp;

  if (fancy_typecheck("sS", arg, "-", "This command subtracts "
		      "its arguments.")) {
    gc_free(ret);
    return NULL;
  }


  tmp = ls_data(arg);

  tot = do_atoi(tmp, &err, 0);

  if (err) {
    error("esh: -: \"-\" only accepts numeric arguments.");
    gc_free(ret);
    return NULL;
  }

  for (arg = ls_next(arg); arg != NULL; arg = ls_next(arg)) {
    tmp = ls_data(arg);

    if (!tmp) continue;

    tot -= do_atoi(tmp, &err, 0);

    if (err) {
      error("esh: -: \"-\" only accepts numeric arguments.");
      gc_free(ret);
      return NULL;
    }
  }

  sprintf(ret, "%d", tot);

  return ls_cons(ret, NULL);
}


static list* over(list* arg) {
  int tot = 1;
  int err;
  char* ret = (char*)gc_alloc(sizeof(char) * 80, "over");
  char* tmp;


  if (fancy_typecheck("sS", arg, "/", "This command divides "
		      "its arguments.")) {
    gc_free(ret);
    return NULL;
  }

  tmp = ls_data(arg);

  tot = do_atoi(tmp, &err, 0);

  if (err) {
    error("esh: /: \"/\" only accepts numeric arguments.");
    gc_free(ret);
    return NULL;
  }

  for (arg = ls_next(arg); arg != NULL; arg = ls_next(arg)) {
    tmp = ls_data(arg);

    if (!tmp) continue;

    tot /= do_atoi(tmp, &err, 1);

    if (err) {
      error("esh: /: \"/\" only accepts numeric arguments.");
      gc_free(ret);
      return NULL;
    }
  }

  sprintf(ret, "%d", tot);

  return ls_cons(ret, NULL);
}


static list* my_eval(list* arg) {
  if (fancy_typecheck("*", arg, "eval",
		      "This command will evaluate each given list as if "
		      "each was a command.\nString values will be simply "
		      "copied and returned.\n")) {
    return NULL;
  }

  return eval(arg);
}


static job_t* nth_job(int i) {
  int j;
  list* iter = jobs;

  for (j = 0; j < i; j++) {
    if (!iter) return NULL;

    iter = ls_next(iter);
  }

  return ls_data(iter);
}


static list* fg(list* arg) {
  if (arg &&
      fancy_typecheck("s", arg, "fg",
		      "This command brings a job into the foreground.\n"
		      "The optional argument specifies which job number "
		      "to use, as given by (jobs).\nIf without arguments, "
		      "the first job will be used.")) {
    return NULL;
  }

  if (!jobs) {
    error("esh: fg: no jobs are running.");

  } else {
    job_t* job;
    int i = 0, err = 0;

    if (arg) {
      i = do_atoi(ls_data(arg), &err, i);

      if (err) {
	error("esh: fg: \"fg\" accepts only a numeric argument.");
	return NULL;
      }
    }

    job = nth_job(i);

    if (!job) {
      error("esh: fg: invalid job number.");
      return NULL;
    }

    job_foreground(job);
  }

  return NULL;
}




static list* bg(list* arg) {
  if (arg &&
      fancy_typecheck("s", arg, "bg",
		      "This command brings a job into the background.\n"
		      "The optional argument specifies which job number "
		      "to use, as given by (jobs).\nIf without arguments, "
		      "the first job will be used.")) {
    return NULL;
  }

  if (!jobs) {
    error("esh: bg: no jobs are running.");

  } else {
    job_t* job;
    int i = 0, err = 0;

    if (arg) {
      i = do_atoi(ls_data(arg), &err, i);

      if (err) {
	error("esh: bg: \"bg\" accepts only a numeric argument.");
	return NULL;
      }
    }

    job = nth_job(i);

    if (!job) {
      error("esh: bg: invalid job number.");
      return NULL;
    }

    job_background(job);
  }

  return NULL;
}



static list* list_jobs(list* arg) {
  list* iter;
  job_t* job;
  int i = 0;

  if (fancy_typecheck("", arg, "jobs",
		      "This command will list all running jobs.")) {
    return NULL;
  }

  printf("No. %-35s %-6s %-6s %-8s\n", "Name", "PID", "PGID", "Status");

  for (iter = jobs; iter != NULL; iter = ls_next(iter), i++) {
    job = ls_data(iter);

    printf("%-3d %-35s %-6d %-6d %-8s\n", i, job->name, job->last_pid,
	   job->pgid,
	   (job->status == JOB_STOPPED ? "Stopped" :
	    (job->status == JOB_DEAD ? "Dead" :
	     "Running")));
  }

  return NULL;
}


static list* defined_p(list* arg) {
  list* ret;

  if (fancy_typecheck("s", arg, "defined?",
		      "This command will return \"true\" if the string "
		      "has\n been defined as a command using "
		      "\"define\". Otherwise, return \"true\".")) {
    return NULL;
  }

  ret = hash_get(defines, ls_data(arg));

  if (ret) {
    return ls_copy(ls_true);

  } else {
    return ls_copy(ls_false);
  }
}


static list* my_interactive(list* arg) {
  if (fancy_typecheck("", arg, "interactive?",
		      "This command returns \"true\" if the shell has "
		      "been\nstarted in interactive mode.")) {
    return NULL;
  }

  if (interactive) {
    return ls_copy(ls_true);

  } else {
    return ls_copy(ls_false);
  }
}



static list* define(list* arg) {
  list* copy = NULL;

  list* old;

  if (fancy_typecheck("s*", arg, "define",
		      "This command will create a new command.\n"
		      "The first argument is the name, and the rest are "
		      "arguments that will be\nautomatically passed to "
		      "\"eval\" whenever the new command gets run.")) {
    return NULL;
  }

  copy = ls_copy(arg);

  old = hash_put(defines, ls_data(copy), ls_next(copy));

  if (old) {
    gc_free(ls_data(copy));
    ls_free_all(old);
  }

  gc_free(copy);

  return NULL;
}


static list* set_prompt(list* arg) {

  if (fancy_typecheck("*", arg, "prompt",
		      "This command will set the prompt to the "
		      "concatenation of the\n\"eval\" of each argument.")) {
    return NULL;
  }

  if (prompt) {
    ls_free_all(prompt);
  }

  prompt = ls_copy(arg);

  return NULL;
}


static list* my_if(list* arg) {
  list* iter;
  list* tmp;
  list* arg_split[3];
  int i;

  if (fancy_typecheck("???", arg, "if",
		      "If the \"eval\" of the first argument is a "
		      "\"true\", this command "
		      "returns the\n\"eval\" of the second argument; "
		      "otherwise, the \"eval\"\nof the third argument is "
		      "returned.")) {
    return NULL;
  }

  arg = ls_copy(arg);

  for (i = 0, iter = arg; iter != NULL; iter = ls_next(iter), i++) {

    arg_split[i] = ls_cons(ls_data(iter), NULL);
    ls_type_set(arg_split[i], ls_type(iter));
    ls_flag_set(arg_split[i], ls_flag(iter));
  }

  ls_free_shallow(arg);

  iter = eval(arg_split[0]);

  if (iter && ls_type(iter) == TYPE_BOOL && !ls_data(iter)) {
    tmp = eval(arg_split[2]);

  } else {
    tmp = eval(arg_split[1]);
  }

  ls_free_all(iter);
  ls_free_all(arg_split[0]);
  ls_free_all(arg_split[1]);
  ls_free_all(arg_split[2]);

  return tmp;
}


static list* equal_p(list* arg) {
  if (fancy_typecheck("ss", arg, "=",
		      "This comand checks is two strings are equal.\n"
		      "If yes, return \"true\".\n"
		      "Otherwise, return \"false\".")) {
    return NULL;
  }

  if (strcmp(ls_data(arg), ls_data(ls_next(arg))) == 0) {
    return ls_copy(ls_true);

  } else {
    return ls_copy(ls_false);
  }
}


static list* pop(list* arg) {
  list* foo;
  list* ret;

  if (fancy_typecheck("", arg, "pop",
		      "This command will pop off a value from the local "
		      "variable stack.")) {
    return NULL;
  }

  if (!stack) return NULL;

  foo = stack;
  stack = ls_next(stack);

  ret = ls_cons(ls_data(foo), NULL);
  ls_type_set(ret, ls_type(foo));
  ls_flag_set(ret, ls_flag(foo));

  gc_free(foo);

  return ret;
}


static list* push(list* arg) {
  if (fancy_typecheck("?", arg, "push",
		      "This command will push on a value to the local "
		      "variable stack.")) {
    return NULL;
  }

  arg = ls_copy(arg);

  stack = ls_cons(ls_data(arg), stack);
  ls_type_set(stack, ls_type(arg));
  ls_flag_set(stack, ls_flag(arg));

  gc_free(arg);

  return NULL;
}


static list* top(list* arg) {
  if (fancy_typecheck("", arg, "top",
		      "This command will return the top value on the "
		      "local variable\nstack, without popping it off.")) {
    return NULL;
  }

  return car(stack);
}


static list* my_list(list* arg) {
  list* ret;
  list* tmp;

  if (fancy_typecheck("*", arg, "list",
		      "This command simply returns a list composed of the "
		      "given arguments.")) {
    return NULL;
  }

  tmp = ls_copy(arg);
  ret = ls_cons(tmp, NULL);
  ls_type_set(ret, TYPE_LIST);

  return ret;
}


static list* reverse(list* arg) {
  if (fancy_typecheck("*", arg, "reverse",
		      "This command returns the arguments in reverse "
		      "order.")) {
    return NULL;
  }

  return ls_reverse(ls_copy(arg));
}


static list* my_stack(list* arg) {
  if (fancy_typecheck("", arg, "stack",
		      "This command will return the local variable "
		      "stack,\ntop values first.")) {
    return NULL;
  }

  return ls_copy(stack);
}


static list* my_print(list* arg) {
  if (fancy_typecheck("*", arg, "print",
		      "This command prints the given arguments in a\n"
		      "human-readable format.")) {
    return NULL;
  }

  ls_print(arg);
  return NULL;
}


static list* my_hash_make(list* arg) {
  hash_table* ntab;
  list* ret;

  if (fancy_typecheck("", arg, "hash-make",
		      "This command will return a new hash table.")) {
    return NULL;
  }

  ntab = (hash_table*)gc_alloc(sizeof(hash_table), "my_hash_make");

  hash_init(ntab, NULL);

  ret = ls_cons(ntab, NULL);
  ls_type_set(ret, TYPE_HASH);

  return ret;
}



static list* my_hash_get(list* arg) {
  if (fancy_typecheck("hs", arg, "hash-get",
		      "This command will return the value associated with the "
		      "given\nkey in the given hash table.")) {
    return NULL;
  }

  return ls_copy(hash_get(ls_data(arg), ls_data(ls_next(arg))));
}

static list* my_hash_put(list* arg) {
  list* copy;
  list* old;
  hash_table* tab;
  char* key;

  if (fancy_typecheck("hs*", arg, "hash-put",
		      "Associate the given data to the given key in "
		      "the\ngiven hash table.")) {
    return NULL;
  }

  /*
   * Note the meaning of "hash_put_inc_ref": it will set the reference
   * count of the newly allocated data equal to the reference count of
   * the rest of the table.
   *
   * For tables that always have a refcount of 1, this is unnecessary,
   * but for tables that get passed back and forth between commands, this
   * is vital to keep the table from imploding.
   */

  tab = ls_data(arg);
  key = ls_data(ls_next(arg));
  copy = ls_copy(ls_next(ls_next(arg)));

  gc_inc_ref(key);

  old = hash_put_inc_ref(tab, key, copy);

  if (old) {
    gc_free(key);
    ls_free_all(old);
  }

  return NULL;
}


static list* my_hash_keys(list* arg) {
  if (fancy_typecheck("h", arg, "hash-keys",
		      "Return all the keys in the given hash table.")) {
    return NULL;
  }

  return hash_keys(ls_data(arg));
}

static list* alias_hash(list* arg) {
  list* ret;

  if (fancy_typecheck("", arg, "alias-hash",
		      "Return all the aliases as a hash table.")) {
    return NULL;
  }

  gc_inc_ref(aliases);
  hash_inc_ref(aliases);

  ret = ls_cons(aliases, NULL);
  ls_type_set(ret, TYPE_HASH);

  return ret;
}


static list* my_car(list* arg) {
  if (fancy_typecheck("l", arg, "car",
		      "Simply return the first element of the given list.")) {
    return NULL;
  }

  return car(ls_data(arg));
}

static list* my_car_l(list* arg) {
  if (fancy_typecheck("*", arg, "car-l",
		      "Simply return the first argument.")) {
    return NULL;
  }

  return car(arg);
}

static list* cdr(list* arg) {
  if (fancy_typecheck("l", arg, "cdr",
		      "Simply return the elements after the first one "
		      "in the given list.")) {
    return NULL;
  }

  if (!ls_data(arg)) return NULL;

  return ls_copy(ls_next(ls_data(arg)));
}

static list* script(list* arg) {
  if (fancy_typecheck("s", arg, "script",
		      "Read the contents of the file named by the "
		      "given string,\nand execute them as a script.")) {
    return NULL;
  }

  do_file(ls_data(arg), 1);

  return NULL;
}

static list* my_read(list* arg) {
  char* rl_out;

  if (fancy_typecheck("s", arg, "read",
		      "Read a line of input from the user.\n"
		      "The first argument is the prompt to show the user.")) {
    return NULL;
  }

  if (!interactive) return NULL;

  rl_out = read_read(ls_data(arg));

  if (!rl_out) return NULL;

  return ls_cons(rl_out, NULL);
}

static list* squish(list* arg) {
  char* buff;

  if (fancy_typecheck("*", arg, "squish",
		      "Concatenate all the given arguments (whether strings "
		      "or lists)\nand combine all the string values into one "
		      "long string.\nList structures have no effect "
		      "on the final output.\n"
		      "Example: (squish foo ~(bar (baz)) => \"foobarbaz\"")) {
    return NULL;
  }

  buff = ls_strcat(arg);

  return ls_cons(buff, NULL);
}


static list* my_parse(list* arg) {
  list* ret;
  int i = 0;
  char* input;

  int len = 128;
  char* value = (char*)gc_alloc(sizeof(char) * len, "my_parse");


  if (fancy_typecheck("s", arg, "parse",
		      "Parse the given string as if it were typed into "
		      "the shell.")) {
    return NULL;
  }

  input = ls_data(arg);

  ret = parse_builtin(input, &i, 0, 0);

  if (next_token(input, &i, &value, &len)) {
    error("esh: extraneous characters after command.");

    ls_free_all(ret);
    ret = NULL;
  }

  gc_free(value);

  return ret;
}


static list* newline(list* arg) {
  if (fancy_typecheck("", arg, "newline",
		      "Simply return the newline character.")) {
    return NULL;
  }

  return ls_cons(dynamic_strcpy("\n"), NULL);
}


static list* my_typecheck(list* arg) {
  int ret;

  if (fancy_typecheck("s*", arg, "typecheck",
		      "This command checks that the types of the given "
		      "arguments are\nwhat you want them to be. "
		      "The first argument is the type specification "
		      "string;\nit must be in the right format.\n"
		      "Please see the manual for a detailed explanation.\n"
		      "The rest of the arguments are to be checked against "
		      "the first one.\n"
		      "This command returns \"false\" if the types of "
		      "the arguments\nmatch.")) {
    return NULL;
  }

  ret = quiet_typecheck(ls_data(arg), ls_next(arg));

  if (!ret) {
    return ls_copy(ls_false);

  } else {
    return ls_copy(ls_true);
  }
}


static list* split(list* arg) {
  list* ret;

  if (fancy_typecheck("S", arg, "split",
		      "This command takes a single string, and returns "
		      "the parts of\nthe original string "
		      "that are separated by the given field separators.\n"
		      "(i.e. the arguments after the first.)\n"
		      "If no field separators are given, split on whitespace."
		      "\n"
		      "Example: (split 'foo bar baz') => foo bar baz")) {
    return NULL;
  }

  if (ls_next(arg)) {
    syntax_blank = ls_strcat(ls_next(arg));
  }

  ret = parse_split(ls_data(arg));

  if (syntax_blank) {
    gc_free(syntax_blank);
    syntax_blank = NULL;
  }

  return ret;
}


static list* unlist(list* arg) {
  if (fancy_typecheck("l", arg, "unlist",
		      "This command simply returns the elements of the "
		      "given list.")) {
    return NULL;
  }

  return ls_copy(ls_data(arg));
}

static list* exec(list* arg) {
  list* oldstack;
  list* ret;

  if (fancy_typecheck("l*", arg, "exec",
		      "This command is equivalent to \"eval\" except "
		      "that the\nstack will be set to the first "
		      "argument while\n\"eval\" is running.")) {
    return NULL;
  }

  oldstack = stack;

  stack = ls_copy(ls_data(arg));
  ret = eval(ls_next(arg));
  ls_free_all(stack);
  stack = oldstack;

  return ret;
}


static list* rot(list* arg) {
  list* foo;

  if (fancy_typecheck("", arg, "rot",
		      "This command switches the top and the next-to-top "
		      "elements of the stack.\nThe element that just "
		      "became the top element is returned.")) {
    return NULL;
  }

  if (quiet_typecheck("?*", stack)) return NULL;

  foo = stack;
  stack = ls_next(ls_next(stack));

  stack = ls_cons(ls_data(foo), stack);
  ls_type_set(stack, ls_type(foo));
  ls_flag_set(stack, ls_flag(foo));

  stack = ls_cons(ls_data(ls_next(foo)), stack);
  ls_type_set(stack, ls_type(ls_next(foo)));
  ls_flag_set(stack, ls_flag(ls_next(foo)));

  gc_free(ls_next(foo));
  gc_free(foo);

  return car(stack);
}


static list* list_stack(list* arg) {
  list* ret;

  if (fancy_typecheck("", arg, "l-stack",
		      "This command will return the local variable "
		      "stack,\nas a list, top values first.")) {
    return NULL;
  }

  ret = ls_cons(ls_copy(stack), NULL);
  ls_type_set(ret, TYPE_LIST);

  return ret;
}

static list* list_cdr(list* arg) {
  list* ret;

  if (fancy_typecheck("l", arg, "l-cdr",
		      "Simply return the elements after the first one "
		      "in the given list,\nas a list.\n"
		      "This command is equivalent to (list (cdr ...)).")) {
    return NULL;
  }

  if (!ls_data(arg)) return NULL;

  ret = ls_cons(ls_copy(ls_next(ls_data(arg))), NULL);
  ls_type_set(ret, TYPE_LIST);

  return ret;
}

static list* my_null(list* arg) {
  return NULL;
}

static list* my_null_p(list* arg) {
  if (fancy_typecheck("?", arg, "null?",
		      "This command returns \"true\" if the argument "
		      "is an empty list.")) {
    return NULL;
  }

  if (!ls_data(arg)) {
    return ls_copy(ls_true);
  } else {
    return ls_copy(ls_false);
  }
}

static list* my_not_null_p(list* arg) {
  if (fancy_typecheck("?", arg, "not-null?",
		      "This command returns \"true\" if the argument "
		      "is NOT an empty list.")) {
    return NULL;
  }

  if (!ls_data(arg)) {
    return ls_copy(ls_false);
  } else {
    return ls_copy(ls_true);
  }
}

static list* my_true(list* arg) {
  return ls_copy(ls_true);
}

static list* my_false(list* arg) {
  return ls_copy(ls_false);
}

static list* my_file_open(list* arg) {
  int* ret = (int*)gc_alloc(sizeof(int) * 2, "my_file_open");

  char* first;
  char* name;

  list* foo = NULL;

  if (fancy_typecheck("ss", arg, "file-open",
		      "This command opens a file.\n"
		      "The first argument describes what type of "
		      "file to open.\n"
		      "Possible values are:\n"
		      "\"file\"     -- Open a regular file for "
		      "reading/writing.\n"
		      "\"truncate\" -- Open a regular file for "
		      "reading/writing, truncating it first.\n"
		      "\"append\"   -- Open a regular file for "
		      "reading/appending.\n"
		      "\"string\"   -- Simulate a file with a string "
		      "variable.\n\n"
		      "The second argument is either a filename or the "
		      "initial value of the\nstring buffer.\n"
		      "This command returns a file, or an empty list "
		      "on error.")) {
    gc_free(ret);
    return NULL;
  }

  /* Note: The first fd is for reading, the second is for writing.
   *       in other words, stdin is [0], stdout is [1]. */

  ret[0] = -1;
  ret[1] = -1;

  first = ls_data(arg);
  name = ls_data(ls_next(arg));

  switch (first[0]) {
  case 'f':
    ret[0] = open(name, O_RDWR | O_CREAT, 0644);
    ret[1] = ret[0];
    break;

  case 't':
    ret[0] = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644);
    ret[1] = ret[0];
    break;

  case 'a':
    ret[0] = open(name, O_RDWR | O_CREAT | O_APPEND, 0644);
    ret[1] = ret[0];
    break;

  case 's':
    if (pipe(ret) < 0) {
      ret[0] = -1;
      ret[1] = -1;

    } else {
      file_write(ret[1], name);
    }
    break;

  default:
    error("esh: file-open: don't know how to open a file using \"%s\".",
	  first);
    gc_free(ret);
    return NULL;
    break;
  }

  if (ret[0] < 0 || ret[1] < 0) {
    error("esh: file-open: couldn't open \"%s\" with mode \"%s\".",
	  name, first);
    gc_free(ret);
    return NULL;
  }

  fcntl(ret[0], F_SETFD, 1);
  fcntl(ret[1], F_SETFD, 1);

  foo = ls_cons(ret, NULL);
  ls_type_set(foo, TYPE_FD);
  return foo;
}


static list* my_file_read(list* arg) {
  int* fd;
  list* ret;

  int flags;

  if (fancy_typecheck("f", arg, "file-read",
		      "This command returns the entire contents of the "
		      "given file,\nas a single string.")) {
    return NULL;
  }

  fd = ls_data(arg);

  flags = fcntl(fd[0], F_GETFL);

  fcntl(fd[0], F_SETFL, flags | O_NONBLOCK);

  ret = ls_cons(file_read(fd[0]), NULL);

  fcntl(fd[0], F_SETFL, flags);

  return ret;
}

static list* my_file_read_block(list* arg) {
  int* fd;
  list* ret;

  if (fancy_typecheck("f", arg, "file-read-block",
		      "This command returns the entire contents of the "
		      "given file,\nas a single string.\n"
		      "This command differs from \"file-read\" "
		      "in that it will\nwait until the whole "
		      "file is read.\n"
		      "Use with caution, as this could cause the "
		      "shell to enter\nan infinite loop.")) {
    return NULL;
  }

  fd = ls_data(arg);

  ret = ls_cons(file_read(fd[0]), NULL);

  return ret;
}

static list* my_file_write(list* arg) {
  int* fd;
  int flags;

  if (fancy_typecheck("fs", arg, "file-write",
		      "This command writes the second argument into the "
		      "first argument.")) {
    return NULL;
  }

  fd = ls_data(arg);

  flags = fcntl(fd[0], F_GETFL);

  fcntl(fd[1], F_SETFL, flags | O_NONBLOCK);

  file_write(fd[1], ls_data(ls_next(arg)));

  fcntl(fd[1], F_SETFL, flags);

  return NULL;
}

static list* my_file_type(list* arg) {
  struct stat sbuff;

  if (fancy_typecheck("s", arg, "file-type",
		      "This command returns a string describing what "
		      "the given file is.\n"
		      "If the file does not exist, this command returns "
		      "\"false\".\n"
		      "Otherwise, one of these strings is returned:\n"
		      "\"link\"      - File is a symbolic link.\n"
		      "\"regular\"   - File is a regular file.\n"
		      "\"directory\" - File is a directory.\n"
		      "\"character\" - File is a character device.\n"
		      "\"block\"     - File is a block device.\n"
		      "\"pipe\"      - File is a FIFO pipe.\n"
		      "\"socket\"    - File is a socket.\n")) {
    return NULL;
  }

  if (lstat(ls_data(arg), &sbuff)) {
    return ls_copy(ls_false);
  }

  if (S_ISLNK(sbuff.st_mode)) {
    return ls_cons(dynamic_strcpy("link"), NULL);

  } else if (S_ISREG(sbuff.st_mode)) {
    return ls_cons(dynamic_strcpy("regular"), NULL);

  } else if (S_ISDIR(sbuff.st_mode)) {
    return ls_cons(dynamic_strcpy("directory"), NULL);

  } else if (S_ISCHR(sbuff.st_mode)) {
    return ls_cons(dynamic_strcpy("character"), NULL);

  } else if (S_ISBLK(sbuff.st_mode)) {
    return ls_cons(dynamic_strcpy("block"), NULL);

  } else if (S_ISFIFO(sbuff.st_mode)) {
    return ls_cons(dynamic_strcpy("pipe"), NULL);

  } else if (S_ISSOCK(sbuff.st_mode)) {
    return ls_cons(dynamic_strcpy("socket"), NULL);

  } else {
    return NULL;
  }
}

static list* standard(list* arg) {
  if (fancy_typecheck("", arg, "standard",
		      "This command returns the standard input/standard "
		      "output file.")) {
    return NULL;
  }

  return ls_copy(ls_stdio);
}


static list* and(list* arg) {
  list* iter;
  list* tmp1 = NULL;
  list* tmp2 = NULL;

  if (fancy_typecheck("*", arg, "and",
		      "This command returns \"false\" if any argument "
		      "is \"false\".\n"
		      "Note: arguments should be quoted with a tilde!")) {
    return NULL;
  }

  for (iter = arg; iter != NULL; iter = ls_next(iter)) {
    ls_free_all(tmp1);
    ls_free_all(tmp2);

    tmp1 = car(iter);
    tmp2 = eval(tmp1);

    if (tmp2 && ls_type(tmp2) == TYPE_BOOL && !ls_data(tmp2)) {
      ls_free_all(tmp1);
      ls_free_all(tmp2);
      return ls_copy(ls_false);
    }
  }

  ls_free_all(tmp1);
  return tmp2;
}



static list* or(list* arg) {
  list* iter;
  list* tmp1;
  list* tmp2;

  if (fancy_typecheck("*", arg, "or",
		      "This command returns \"false\" if all arguments "
		      "are \"false\".\n"
		      "Note: arguments should be quoted with a tilde!")) {
    return NULL;
  }

  for (iter = arg; iter != NULL; iter = ls_next(iter)) {
    tmp1 = car(iter);
    tmp2 = eval(tmp1);

    if (!tmp2 || ls_type(tmp2) != TYPE_BOOL || ls_data(tmp2)) {
      ls_free_all(tmp1);
      return tmp2;
    }

    ls_free_all(tmp1);
    ls_free_all(tmp2);
  }

  return ls_copy(ls_false);
}

static list* not(list* arg) {
  if (fancy_typecheck("b", arg, "not",
		      "This command returns \"false\" if the argument is "
		      "\"true\".")) {
    return NULL;
  }

  if (ls_data(arg)) {
    return ls_copy(ls_false);

  } else {
    return ls_copy(ls_true);
  }
}



static list* begin_last(list* arg) {
  list* iter;
  list* tmp1 = NULL;
  list* tmp2 = NULL;

  if (fancy_typecheck("*", arg, "begin-last",
		      "This command evaluates the given argument, one "
		      "by one,\nand returns the value of the last "
		      "argument.")) {
    return NULL;
  }

  for (iter = arg; iter != NULL; iter = ls_next(iter)) {
    tmp1 = car(iter);
    tmp2 = eval(tmp1);

    if (!ls_next(iter)) {
      ls_free_all(tmp1);
      return tmp2;
    }

    ls_free_all(tmp1);
    ls_free_all(tmp2);
  }

  return NULL;
}


static list* version(list* arg) {
  list* ret = NULL;
  char* tmp;

  if (fancy_typecheck("", arg, "version",
		      "This command returns the version of the shell, "
		      "as three numbers.")) {
    return NULL;
  }

  tmp = (char*)gc_alloc(sizeof(char) * 5, "version");
  sprintf(tmp, "%d", VERSION_MAJOR);
  ret = ls_cons(tmp, ret);

  tmp = (char*)gc_alloc(sizeof(char) * 5, "version");
  sprintf(tmp, "%d", VERSION_MINOR);
  ret = ls_cons(tmp, ret);

  tmp = (char*)gc_alloc(sizeof(char) * 5, "version");
  sprintf(tmp, "%d", VERSION_PATCH);
  ret = ls_cons(tmp, ret);

  return ls_reverse(ret);
}


static list* builtin(list* arg) {
  list* (*func)(list*);

  if (fancy_typecheck("s*", arg, "builtin",
		      "This command executes the first argument as if it "
		      "was a\nbuiltin command, regardless of whether it has "
		      "been overriden or not.\nIt is useful for writing your "
		      "own replacements for builtin commands.")) {
    return NULL;
  }

  func = hash_get(builtins, ls_data(arg));

  if (!func) {
    error("esh: builtin: %s is not a command.", ls_data(arg));
    return NULL;
  }

  return func(ls_next(arg));
}


static list* begin(list* arg) {
  return ls_copy(arg);
}


static list* stderr_handler(list* arg) {
  int* fd;

  if (fancy_typecheck("f", arg, "stderr-handler",
		      "This command will set the standard error handler.\n"
		      "When the standard error handler is set, all new "
		      "subprocesses\nwill use the given file as the "
		      "standard error.\n"
		      "For example, to save all standard error in a file "
		      "called\n\"stderr.log\" in your home directory, "
		      "run this command:\n"
		      "\"(stderr-handler \n"
		      "          (file-open file (squish (get HOME) "
		      "sterr.log)))\"")) {
    return NULL;
  }

  fd = ls_data(arg);

  if (stderr_handler_fd != STDERR_FILENO &&
      stderr_handler_fd != STDOUT_FILENO) {
    close(stderr_handler_fd);
  }

  stderr_handler_fd = fd[1];
  return NULL;
}


static list* my_stderr(list* arg) {
  if (fancy_typecheck("", arg, "stderr",
		      "This command returns the standard input/standard error "
		      "file.")) {
    return NULL;
  }

  return ls_copy(ls_stderr);
}


static list* my_wait(list* arg) {
  int err;
  long val = 0;

  if (fancy_typecheck("s", arg, "wait",
                      "This command will pause for the given number of "
                      "seconds.")) {
    return NULL;
  }

  val = do_atoi(ls_data(arg), &err, val);

  if (err) {
    error("esh: wait: \"wait\" takes a numeric value.");
    return NULL;
  }

  sleep(val);
  return NULL;
}


static list* alive_p(list* arg) {
  pid_t foo;
  char buff[255];
  struct stat dummy;

  if (fancy_typecheck("p", arg, "alive?",
		      "This command returns \"true\" if the given process "
		      "is still running,\nor \"false\" otherwise.")) {
    return NULL;
  }

  foo = *(pid_t*)(ls_data(arg));

  sprintf(buff, "/proc/%d", foo);

  /* Mondo-hack: check the /proc filesystem to see if the process exists. */

  if (stat(buff, &dummy)) {
    return ls_copy(ls_false);

  } else {
    return ls_copy(ls_true);
  }
}


static list* my_while(list* arg) {
  list* cond;
  list* act;
  list* foo;
  list* oldstack;

  if (fancy_typecheck("ll*", arg, "while",
		      "This command will iteratively \"eval\" the second "
		      "argument\nas long as the \"eval\" of the first "
		      "argument is not \"false\".\n"
		      "The rest of the arguments define the initial "
		      "stack for\nthe duration of execution of \"while\".\n"
		      "This command is only suitable when you don't "
		      "care about\nthe return value of the second "
		      "argument.")) {
    return NULL;
  }

  oldstack = stack;

  stack = ls_copy(ls_next(ls_next(arg)));

  cond = car(arg);
  act = car(ls_next(arg));

  while (1) {
    foo = eval(cond);

    if (exception_flag ||
	(foo && ls_type(foo) == TYPE_BOOL && !ls_data(foo))) {

      ls_free_all(foo);
      break;
    }

    ls_free_all(foo);
    ls_free_all(eval(act));
  }

  ls_free_all(stack);
  ls_free_all(cond);
  ls_free_all(act);
  stack = oldstack;

  return NULL;
}


static list* chop(list* arg) {
  char* foo;
  int len, i;

  if (fancy_typecheck("s", arg, "chop!",
		      "Get rid of the last character in the given string.\n"
		      "Warning: the given string is modified in-place!")) {
    return NULL;
  }

  foo = ls_data(arg);
  len = strlen(foo);

  for (i = len-1; i >= 0; i--) {
    if (foo[i] != '\0') {
      foo[i] = '\0';
      return NULL;
    }
  }

  return ls_copy(arg);
}


static list* chop_nl(list* arg) {
  char* foo;
  int len, i;

  if (fancy_typecheck("s", arg, "chop-nl!",
		      "Get rid of the last character in the given string, "
		      "but only if\nit is a newline.\n"
		      "Warning: the given string is modified in-place!")) {
    return NULL;
  }

  foo = ls_data(arg);
  len = strlen(foo);

  for (i = len-1; i >= 0; i--) {
    if (foo[i] == '\0') continue;

    if (foo[i] != '\n') break;

    foo[i] = '\0';
    break;
  }

  return ls_copy(arg);
}


static list* match(list* arg) {
  regex_t reg;
  int err;

  if (fancy_typecheck("ss", arg, "match",
		      "Match the second argument with the first.\n"
		      "The first argument is a regular expression.\n"
		      "This command returns \"true\" if there is a match, "
		      "or \"false\" otherwise.")) {
    return NULL;
  }

  err = regcomp(&reg, ls_data(arg), REG_EXTENDED | REG_NOSUB);

  if (!err) {
    char* tomatch = ls_data(ls_next(arg));

    err = regexec(&reg, tomatch, 0, NULL, 0);

    if (err == REG_NOMATCH) {
      regfree(&reg);
      return ls_copy(ls_false);
    }
  }

  if (err) {
    int len = regerror(err, &reg, NULL, 0);
    char* buff = (char*)gc_alloc(sizeof(char) * len, "match");

    regerror(err, &reg, buff, len);

    error("esh: match: %s", buff);

    gc_free(buff);
    regfree(&reg);
    return NULL;
  }

  regfree(&reg);
  return ls_copy(ls_true);
}


static list* chars(list* arg) {
  char* foo;
  char* bar;
  int len, i;
  list* ret = NULL;

  if (fancy_typecheck("s", arg, "chars",
		      "Return a list of the characters in the given "
		      "string.")) {

    return NULL;
  }

  foo = ls_data(arg);
  len = strlen(foo);

  for (i = len-1; i >= 0; i--) {
    bar = (char*)gc_alloc(sizeof(char) * 2, "chars");

    bar[0] = foo[i];
    bar[1] = '\0';

    ret = ls_cons(bar, ret);
  }

  return ret;
}



static list* filter(list* arg) {
  char* foo;
  char* bar;
  int len, i;
  list* ret = NULL;
  list* tmp;
  list* code;
  list* oldstack = stack;

  if (fancy_typecheck("sl", arg, "filter",
		      "Filter the first argument with the second one.\n"
		      "The characters of the first argument are passed "
		      "to the second one,\ncharacter by character, and then "
		      "the outputs of the second argument\nare passed to "
		      "\"squish\" to form the\nreturn value of this "
		      "command.")) {
    return NULL;
  }

  code = car(ls_next(arg));

  foo = ls_data(arg);
  len = strlen(foo);

  for (i = len-1; i >= 0; i--) {
    bar = (char*)gc_alloc(sizeof(char) * 2, "filter");

    bar[0] = foo[i];
    bar[1] = '\0';

    stack = ls_cons(bar, NULL);

    tmp = eval(code);

    ret = ls_cons(tmp, ret);
    ls_type_set(ret, TYPE_LIST);

    ls_free_all(stack);
  }

  stack = oldstack;

  bar = ls_strcat(ret);

  ls_free_all(ret);
  ls_free_all(code);

  return ls_cons(bar, NULL);
}


static list* my_clone(list* arg) {
  list* ret = NULL;
  char* foo;
  int i, len, err;

  if (fancy_typecheck("ss", arg, "clone",
		      "This command will return the first argument "
		      "X number of times,\nwhere X is equal to the second "
		      "argument.")) {
    return NULL;
  }

  foo = ls_data(arg);

  len = do_atoi(ls_data(ls_next(arg)), &err, 0);

  if (err) {
    error("esh: clone: \"clone\" takes a numeric value.");
    return NULL;
  }

  for (i = 0; i < len; i++) {
    gc_inc_ref(foo);
    ret = ls_cons(foo, ret);
  }

  return ret;
}


static list* substring_p(list* arg) {
  if (fancy_typecheck("ss", arg, "substring?",
		      "This command returns \"true\" if the first argument "
		      "is a\nsubstring of the second.")) {
    return NULL;
  }

  if (strstr(ls_data(ls_next(arg)), ls_data(arg))) {
    return ls_copy(ls_true);
  } else {
    return ls_copy(ls_false);
  }
}


static list* less_than(list* arg) {
  int arg1, arg2;
  int err1, err2;

  if (fancy_typecheck("ss", arg, "<",
		      "This command returns true if the first argument is "
		      "less than\nthe second.")) {
    return NULL;
  }

  arg1 = do_atoi(ls_data(arg), &err1, 0);
  arg2 = do_atoi(ls_data(ls_next(arg)), &err2, 0);

  if (err1 || err2) {
    error("esh: <: \"<\" only accepts numeric arguments.");
    return NULL;
  }

  if (arg1 < arg2) {
    return ls_copy(ls_true);
  } else {
    return ls_copy(ls_false);
  }
}


static list* greater_than(list* arg) {
  int arg1, arg2;
  int err1, err2;

  if (fancy_typecheck("ss", arg, ">",
		      "This command returns true if the first argument is "
		      "greater than\nthe second.")) {
    return NULL;
  }

  arg1 = do_atoi(ls_data(arg), &err1, 0);
  arg2 = do_atoi(ls_data(ls_next(arg)), &err2, 0);

  if (err1 || err2) {
    error("esh: >: \">\" only accepts numeric arguments.");
    return NULL;
  }

  if (arg1 > arg2) {
    return ls_copy(ls_true);
  } else {
    return ls_copy(ls_false);
  }
}


static list* my_void(list* arg) {
  return ls_copy(ls_void);
}


static list* repeat(list* arg) {
  int arg1, err1, i;

  if (fancy_typecheck("s*", arg, "repeat",
		      "This command evaluates the given arguments some number "
		      "of times and\nreturns nothing. The first argument "
		      "specifies the number\nof times the rest of the "
		      "arguments should be evaluated.")) {
    return NULL;
  }

  arg1 = do_atoi(ls_data(arg), &err1, 0);

  if (err1) {
    error("esh: repeat: expected a number as first argment.");
    return NULL;
  }

  for (i = 0; i < arg1; i++) {
    ls_free_all(eval(ls_next(arg)));
  }

  return NULL;
}



hash_entry builtins_array[] = {
  { "cd",     cd },
  { "help",   help },
  { "copy",   ls_copy },
  { "set",    set },
  { "get",    get },
  { "env",    env },
  { "run",    run },
  { "run-simple", run_simple },
  { "gobble", gobble },
  { "exit",   my_exit },
  { "alias",  alias },
  { "+",      plus },
  { "*",      times },
  { "-",      minus },
  { "/",      over },
  { "eval",  my_eval },
  { "fg",     fg },
  { "bg",     bg },
  { "jobs",   list_jobs },
  { "define", define },
  { "prompt", set_prompt },
  { "if",     my_if },
  { "=",      equal_p },
  { "pop",    pop },
  { "push",   push },
  { "top",    top },
  { "list",   my_list },
  { "print",  my_print },
  { "stack",  my_stack },
  { "hash-make", my_hash_make },
  { "hash-get",  my_hash_get },
  { "hash-put",  my_hash_put },
  { "hash-keys", my_hash_keys },
  { "car",       my_car },
  { "first",     my_car },
  { "cdr",       cdr },
  { "rest",      cdr },
  { "script",    script },
  { "read",      my_read },
  { "squish",    squish },
  { "parse",     my_parse },
  { "newline",   newline },
  { "nl",        newline },
  { "typecheck", my_typecheck },
  { "split",     split },
  { "unlist",    unlist },
  { "exec",      exec },
  { "rot",       rot },
  { "l-stack",   list_stack },
  { "begin",     begin },
  { "defined?",  defined_p },
  { "null",      my_null },
  { "file-open", my_file_open },
  { "file-read", my_file_read },
  { "file-read-block", my_file_read_block },
  { "file-write", my_file_write },
  { "file-type", my_file_type },
  { "standard",  standard },
  { "stderr",    my_stderr },
  { "interactive?", my_interactive },
  { "and",       and },
  { "or",        or },
  { "not",       not },
  { "version",   version },
  { "builtin",   builtin },
  { "true",      my_true },
  { "false",     my_false },
  { "null?",     my_null_p },
  { "not-null?", my_not_null_p },
  { "l-cdr",     list_cdr },
  { "l-rest",    list_cdr },
  { "stderr-handler", stderr_handler },
  { "wait",      my_wait },
  { "alive?",    alive_p },
  { "while",     my_while },
  { "alias-hash", alias_hash },
  { "car-l",     my_car_l },
  { "first-l",   my_car_l },
  { "chop!",     chop },
  { "chop-nl!",  chop_nl },
  { "match",     match },
  { "reverse",   reverse },
  { "chars",     chars },
  { "filter",    filter },
  { "clone",     my_clone },
  { "substring?", substring_p },
  { "begin-last", begin_last },
  { "<",         less_than },
  { ">",         greater_than },
  { "void",      my_void },
  { "repeat",    repeat },
  { NULL, NULL }
};

