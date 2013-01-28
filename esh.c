
/* 
 * esh, the Unix shell with Lisp-like syntax. 
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include <errno.h>

#include <setjmp.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include <glob.h>

#include "common.h"
#include "format.h"
#include "list.h"
#include "gc.h"
#include "hash.h"
#include "job.h"
#include "builtins.h"
#include "read.h"



int shell_terminal_fd = STDIN_FILENO;
int stderr_handler_fd = STDERR_FILENO;

int interactive = 0;
int exception_flag = 0;

struct termios shell_terminal_modes;

hash_table* builtins;
hash_table* aliases;
hash_table* defines;

list* jobs = NULL;
list* prompt = NULL;
list* stack = NULL;
list* ls_true = NULL;
list* ls_false = NULL;
list* ls_stdio = NULL;
list* ls_stderr = NULL;
list* ls_void = NULL;

char* syntax_blank = NULL;
char* syntax_special = NULL;
int syntax_fancy = 1;

char** environ;

int blank(char c) {
  if (c && strchr((syntax_blank ? syntax_blank : " \t\n"), c)) return 1;
  return 0;
}

int openparen(char c) {
  if (c && strchr("(", c)) return 1;
  return 0;
}

int closeparen(char c) {
  if (c && strchr(")", c)) return 1;
  return 0;
}

int separator(char c) {
  if (syntax_fancy && c && strchr(",|", c)) return 1;
  return 0;
}

int redirect_in(char c) {
  if (syntax_fancy && c && strchr("<", c)) return 1;
  return 0;
}

int redirect_out(char c) {
  if (syntax_fancy && c && strchr(">", c)) return 1;
  return 0;
}

int quote(char c) {
  if (c && strchr("\"", c)) return 2;
  if (c && strchr("'", c)) return 1;
  return 0;
}

int literal(char c) {
  if (c && strchr("`\\", c)) return 1;
  return 0;
}

int delaysym(char c) {
  if (c && strchr("$~", c)) return 1;
  return 0;
}

int comment(char c) {
  if (c && strchr("#", c)) return 1;
  return 0;
}

int special(char c) {
  if (syntax_special) {
    if (strchr(syntax_special, c))
      return 1;

    return 0;

  } else if (openparen(c) || closeparen(c) || separator(c) || 
	     redirect_in(c) || redirect_out(c) || quote(c) ||
	     literal(c) || delaysym(c) || comment(c) ||
	     c == '\0') {
    return 1;
  }

  return 0;
}

char* dynamic_strcpy(char* str) {
  char* tmp = (char*)gc_alloc(sizeof(char) * (strlen(str) + 1), 
			      "dynamic_strcpy");

  strcpy(tmp, str);

  return tmp;
}


/*
 * The tokenizer. Useful, but quite limited. It does not detect numerals
 * or list literals. In fact, it only detects strings and special 
 * symbols. 
 *
 * List literals are created by the parser, since they could well 
 * have special syntax.
 *
 * Detecting numerals is the job of the individual commands. This is
 * done because numerals are needed so infrequently.
 */

char next_token(char* input, int* i, char** token_value, int* len) {
  char ret, foo;
  int currquote = 0;
  int do_write = 0;
  int ignore = 0;
  int j = 0;

  if (!input || !input[*i]) {
    return '\0';
  }

  while (1) {
    foo = input[*i];

    if (ignore) {
      if (foo == '\n' || !foo) {
	ignore = 0;
      }
    }

    /* Are we currently in a quote? */
    if (!ignore && quote(foo)) {
      if (do_write) {
	ret = 'a';
	break;

      } else if (!currquote) {
	currquote = quote(foo);

      } else if (currquote == quote(foo)) {
	ret = 'a';
	(*i)++;
	break;
      }
    }

    /* End of input. */
    if (!foo) {
      if (do_write) {
	ret = 'a';
      } else {
	ret = '\0';
      }

      break;
    }

    /* Handle comments. */
    if (!ignore && comment(foo) && !currquote) {
      if (do_write) {
	ret = 'a';
	break;

      } else {
	ignore = 1;
      }
    }

    /* Stop at special syntax. */
    if (!ignore && special(foo) && !quote(foo) && !currquote && !do_write) {
      (*i)++;
      ret = foo;
      break;
    }
    
    /* Find a word beginning. */
    if (!ignore && !currquote) {

      /* It's a letter. */
      if (!blank(foo) && !special(foo)) {
       char bar;

       /* Already in a word. */
       if (do_write) {
	 /* Nothing. */

       /* Beginning of string. */
       } else if (!*i) {
	 do_write = 1;

       /* Beginning of word. */
       } else {
	 bar = input[(*i)-1];

	 if (blank(bar) || special(bar)) do_write = 1;
       }

      } else if (do_write) {
	ret = 'a';
	break;
      }
    }

    (*i)++;

    if (do_write || (currquote && currquote != quote(foo))) {
      if (j == (*len)-2) {
	char* tmp = (char*)gc_alloc(sizeof(char) * (*len) * 2,
				    "next_token");
	
	(*token_value)[(*len)-1] = '\0';
	strcpy(tmp, (*token_value));

	gc_free(*token_value);
	(*token_value) = tmp;
	(*len) *= 2;
      }

      (*token_value)[j++] = foo;
    }
  }

  (*token_value)[j] = '\0';

  if (!ret && currquote) {
    error("esh: parse error: end of input while looking for "
	  "a closing quote.");
  }

  return ret;
}



static void ls_strcat_aux(list* ls, char** buff, int* i, int* len) {
  list* iter;
  int foo;

  for (iter = ls; iter != NULL; iter = ls_next(iter)) {

    if (ls_type(iter) == TYPE_LIST) {
      ls_strcat_aux(ls_data(iter), buff, i, len);

    } else if (ls_type(iter) == TYPE_STRING) {
      foo = strlen(ls_data(iter));

      if ((*i) + foo >= ((*len)-1)) {
	char* tmp = (char*)gc_alloc(sizeof(char) * (*len + foo) * 2, 
				    "ls_strcat_aux");

	(*len) = (*len + foo) * 2;

	strcpy(tmp, (*buff));
	gc_free((*buff));
	(*buff) = tmp;
      }
      
      strcat((*buff), ls_data(iter));
      (*i) += foo;
    }
  }
}


char* ls_strcat(list* ls) {
  int i = 0;
  int len = 128;
  char* buff = (char*)gc_alloc(sizeof(char) * len, "ls_strcat");

  buff[0] = '\0';
  
  ls_strcat_aux(ls, &buff, &i, &len);

  return buff;
}


glob_t* globbify(list* command) {
  glob_t* ret = (glob_t*)gc_alloc(sizeof(glob_t), "globbify");

  int flags = GLOB_NOCHECK;
  list* iter;
  list* alias;

  list* junk = NULL;

  if (!command) {
    gc_free(ret);
    return NULL;
  }

  alias = hash_get(aliases, ls_data(command));

  if (alias) {
    if (ls_type(alias) == TYPE_LIST) {
      alias = NULL;

    } else {
      command = ls_next(command);
    }
  }

  for (iter = alias; iter != NULL; iter = ls_next(iter)) {
    
    glob(ls_data(iter), flags, NULL, ret);

    flags |= GLOB_APPEND;
  }

  for (iter = command; iter != NULL; iter = ls_next(iter)) {

    if (ls_type(iter) == TYPE_LIST) {
      iter = eval(iter);
      junk = iter;
    }

    if (ls_type(iter) != TYPE_STRING) {
      error("esh: disk commands should be given as lists of strings.");

      globfree(ret);
      gc_free(ret);
      ls_free_all(junk);
      return NULL;
    }

    glob(ls_data(iter), flags, NULL, ret);

    flags |= GLOB_APPEND;
  }

  ls_free_all(junk);

  return ret;
}

void close_aux(int fd) {
  if (fd != STDIN_FILENO &&
      fd != STDOUT_FILENO &&
      fd != STDERR_FILENO) {
    
    close(fd);
  }
}

void dup2_aux(int old, int new) {
  if (old != new) {
    if (dup2(old, new) < 0) {
      error("esh: I/O redirection failed.");
      exit(EXIT_FAILURE);
    }
    
    close_aux(old);
  }
}

pid_t fork_aux(void) {
  pid_t ret = fork();

  if (ret < 0) {
    error("esh: forking failed.");
    exit(EXIT_FAILURE);
  }

  return ret;
}

int exec_aux(char** comm, pid_t pgid, int in_fd, int out_fd, int err_fd) {
  pid_t pid;

  if (comm == NULL) {
    error("esh: tried to execute a null command.");
    exit(EXIT_FAILURE);
  }

  pid = getpid();

  if (!pgid) pgid = pid;

  setpgid(pid, pgid);

  if (interactive) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
  }

  dup2_aux(in_fd, STDIN_FILENO);
  dup2_aux(out_fd, STDOUT_FILENO);
  dup2_aux(err_fd, STDERR_FILENO);

  execvp(comm[0], comm);

  error("esh: cannot execute \"%s\".", comm[0]);
  exit(EXIT_FAILURE);
}

void pipe_aux(int pipes[2]) {
  if (pipe(pipes)) {
    error("esh: pipe creation failed.");
    exit(EXIT_FAILURE);
  }
}


int open_read_aux(char* file) {
  int fd = open(file, O_RDONLY);

  if (fd < 0) {
    error("esh: cannot open \"%s\" for reading.", file);
    return -1;
  }

  fcntl(fd, F_SETFD, 1);

  return fd;
}

int open_read_error_aux(char* file) {
  int fd = open(file, O_RDONLY);

  fcntl(fd, F_SETFD, 1);

  return fd;
}

int open_write_aux(char* file) {
  int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if (fd < 0) {
    error("esh: cannot open \"%s\" for writing.", file);
    return -1;
  }

  fcntl(fd, F_SETFD, 1);

  return fd;
}


char* file_read(int fd) {
  int i = 0;
  int len = 128;
  char* buff = (char*)gc_alloc(sizeof(char) * len, "file_read");
  char foo;

  while (1) {
    if (read(fd, &foo, 1) <= 0) {
      break;
    }

    if (i >= len-2) {
      char* tmp = (char*)gc_alloc(sizeof(char) * len * 2, "file_read");

      len *= 2;

      strcpy(tmp, buff);
      gc_free(buff);
      buff = tmp;
    }
    
    buff[i] = foo;
    i++;
  }

  buff[i] = '\0';

  return buff;
}

void file_write(int fd, char* data) {
  int i = 0;
  
  while (1) {
    if (!data[i]) break;

    if (write(fd, &data[i], 1) <= 0) {
      break;
    }

    i++;
  }
}

void show_status(job_t* job, int stat) {

  if (WIFEXITED(stat)) {
    job->status = JOB_DEAD;
    job->value = WEXITSTATUS(stat);

  } else if (WIFSTOPPED(stat)) {
    job->status = JOB_STOPPED;

    error_simple("esh: %s: signalled to stop. (", job->name);

    switch (WSTOPSIG(stat)) {
    case SIGSTOP:
      error_simple("Stopped");
      break;

    case SIGTSTP:
      error_simple("Stopped by user");
      break;

    case SIGTTIN:
      error_simple("Tried to access terminal input illegaly");
      break;

    case SIGTTOU:
      error_simple("Tried to access terminal output illegaly");
      break;

    default:
      error_simple("Unknown signal %d", WSTOPSIG(stat));
      break;
    }

    error(")");

  } else if (WIFSIGNALED(stat)) {
    job->status = JOB_DEAD;

    error_simple("esh: %s: signalled to exit. (", job->name);

    switch (WTERMSIG(stat)) {
    case SIGHUP:
      error_simple("Terminal hung up");
      break;

    case SIGINT:
      error_simple("Interrupt");
      break;

    case SIGQUIT:
      error_simple("Quit");
      break;

    case SIGILL:
      error_simple("Illegal instruction");
      break;

    case SIGABRT:
      error_simple("Aborted");
      break;

    case SIGFPE:
      error_simple("Floating point exception");
      break;

    case SIGKILL:
      error_simple("Killed");
      break;

    case SIGSEGV:
      error_simple("Segfaulted");
      break;

    case SIGPIPE:
      error_simple("Broken pipe");
      break;

    case SIGALRM:
      error_simple("Timer went off");
      break;

    case SIGTERM:
      error_simple("Terminated");
      break;

    case SIGBUS:
      error_simple("Bus error");
      break;

    default:
      error_simple("Unknown signal %d", WTERMSIG(stat));
      break;
    }
     
    error(")");
  }

  if (WCOREDUMP(stat)) {
    error("Core was dumped.");
  }
}


/*
 * The argument is a dummy value so that this function can be used
 * as a signal handler.
 */

void babysit(int signum) {
  list* iter;
  job_t* job;
  int stat;
  int tmp;

  for (iter = jobs; iter != NULL; iter = ls_next(iter)) {
    job = ls_data(iter);

    while (1) {
      tmp = waitpid(-job->pgid, &stat, WUNTRACED | WNOHANG);

      if (tmp <= 0) break;

      if (tmp == job->last_pid) {
	show_status(job, stat);
      }

      if (job->status == JOB_DEAD) {
	kill(-job->pgid, SIGPIPE);
	job->status = JOB_DEAD;
      }
    }
  }

  signal(SIGCHLD, babysit);
}


void job_wait(job_t* job) {
  int tmp;
  sig_t oldsig;

  oldsig = signal(SIGCHLD, SIG_DFL);
  if (interactive) {
    waitpid(job->last_pid, &tmp, WUNTRACED);

    if (job->name) {
      show_status(job, tmp);
    }

    if (job->status == JOB_DEAD) {
      kill(-job->pgid, SIGPIPE);

      while (waitpid(-job->pgid, &tmp, WUNTRACED) > 0) {
	/* Nothing. */
      }
    }

  } else {
    waitpid(job->last_pid, &tmp, WUNTRACED);
  }
  signal(SIGCHLD, oldsig);
}


void job_foreground(job_t* job) {

  if (interactive) {
    tcsetpgrp(shell_terminal_fd, job->pgid);

    if (job->status == JOB_STOPPED) {
      tcsetattr(shell_terminal_fd, TCSADRAIN, &job->terminal_modes);
    
      if (kill(-job->pgid, SIGCONT) < 0) {
	error("esh: could not continue PGID %d!", job->pgid);
	return;
      }

    } else if (job->status == JOB_DEAD) {
      error("esh: tried to bring a dead job into the foreground!");
      return;
    }
  }
  
  job->status = JOB_RUNNING;

  job_wait(job);

  if (interactive) {
    tcsetpgrp(shell_terminal_fd, getpid());
    tcgetattr(shell_terminal_fd, &job->terminal_modes);
    tcsetattr(shell_terminal_fd, TCSADRAIN, &shell_terminal_modes);
  }
}


void job_background(job_t* job) {

  if (!interactive) return;

  if (job->status == JOB_STOPPED) {
    if (kill(-job->pgid, SIGCONT) < 0) {
      error("esh: could not continue PGID %d!", job->pgid);
      return;
    }

  } else if (job->status == JOB_DEAD) {
    error("esh: tried to bring a dead job into the background!");
    return;
  }

  job->status = JOB_RUNNING;
}


void arrange_funeral(void) {
  list* jnew = NULL;
  list* iter;
  job_t* job;
  
  int foo = 0;

  if (!interactive) {
    while (waitpid(0, NULL, WUNTRACED | WNOHANG) > 0) {
      /* Nothing. */
    }

    return;
  }

  if (!jobs) return;

  for (iter = jobs; iter != NULL; iter = ls_next(iter)) {
    job = ls_data(iter);
    
    if (job->status == JOB_DEAD) foo = 1;
  }

  if (!foo) return;

  for (iter = jobs; iter != NULL; iter = ls_next(iter)) {
    job = ls_data(iter);
    
    if (job->status == JOB_DEAD) {
      gc_free(job->name);
      gc_free(job);

    } else {
      jnew = ls_cons(job, jnew);
    }
  }

  ls_free(jobs);

  jobs = ls_reverse(jnew);
}



pid_t do_pipe(int f_src, int f_out, list* ls, int bg, int destructive) {
  pid_t pid, pgid = 0, last_pid = 0, ret = 0;
  int pipes[2];
  int input_src = STDIN_FILENO;
  int output_sink;
  
  glob_t* comm = NULL;

  job_t* job = NULL;

  int i;

  job = (job_t*)gc_alloc(sizeof(job_t), "do_pipe");
  job->name = NULL;

  if (!interactive) {
    pgid = getpid();
  }

  input_src = f_src;

  if (ls == NULL) goto done;

  
  for (i = 0; ls != NULL; ls = ls_next(ls), i++) {

    if (ls_type(ls) != TYPE_LIST) {
      error("esh: disk commands should be only passed in the form of lists.");
      goto done;
    }

    comm = globbify(ls_data(ls));

    if (!comm) {
      error("esh: parse error: tried to execute a null command.");
      goto done;

    } 

    if (!job->name) {
      job->name = (char*)gc_alloc(sizeof(char) * 
				(strlen(comm->gl_pathv[0])+1),
				  "do_pipe");
    
      strcpy(job->name, comm->gl_pathv[0]);
    }

    if (ls_next(ls)) {
      pipe_aux(pipes);
      output_sink = pipes[1];

    } else {
      output_sink = f_out;
    }


    pid = fork_aux();

    if (pid == 0) {
      
      exec_aux(comm->gl_pathv, pgid, input_src, output_sink, 
	       stderr_handler_fd);

    } else {
      if (!pgid) pgid = pid;

      last_pid = pid;

      setpgid(pid, pgid);
    }
    
    globfree(comm);
    gc_free(comm);
    comm = NULL;

    if (input_src != f_src) {
      close_aux(input_src);
    }

    if (output_sink != f_out || destructive) {
      close_aux(output_sink);
    }

    input_src = pipes[0];

  }

  job->pgid = pgid;
  job->last_pid = last_pid;
  job->status = JOB_RUNNING;
  job->value = 0;

  if (interactive) {
    jobs = ls_cons(job, jobs);
  }

  if (!bg) {
    job_foreground(job);
    ret = job->value;

  } else {
    ret = last_pid;
  }

  if (!interactive) {
    gc_free(job->name);
    gc_free(job);
  }

  return ret;

  done:

  if (job) {
    if (job->name) {
      gc_free(job->name);
    }

    job->name = NULL;

    if (!bg) {
      job_foreground(job);
    }

    gc_free(job);
  }

  if (comm) {
    globfree(comm);
    gc_free(comm);
  }

  return -1;
}


list* do_builtin(list* ls) {
  list* (*func)(list*);
  list* foo;

  if (ls == NULL || exception_flag) return NULL;

  if (ls_type(ls) != TYPE_STRING) {
    error("esh: command names are always strings.");
    return NULL;
  }
  
  func = hash_get(builtins, ls_data(ls));
  foo = hash_get(defines, ls_data(ls));

  if (!func && !foo) {
    error("esh: %s is not a command.", ls_data(ls));
    return NULL;
  }

  if (foo) {
    list* ret;
    list* oldstack = stack;

    stack = ls_copy(ls_next(ls));
    ret = eval(foo);

    ls_free_all(stack);
    stack = oldstack;

    return ret;

  } else {
    return func(ls_next(ls));
  }
}

list* parse_builtin(char* input, int* i, int liter, int delay) {
  list* ls = NULL;

  char token;
  int len = 128;
  char* value = (char*)gc_alloc(sizeof(char)*len, "parse_builtin");

  int did_pass = 0;
  int pass_liter = 0;
  int pass_delay = 0;
  list* passthru = NULL;
  list* ret = NULL;

  token = next_token(input, i, &value, &len);

  if (!openparen(token)) {
    error("esh: parse error: commands should always use "
	  "parentheses.");
    goto done;
  }

  while (1) {
    did_pass = 0;
    pass_liter = 0;
    pass_delay = 0;
    token = next_token(input, i, &value, &len);

    if (!token) {
      error("esh: parse error: no closing parentheses.");
      goto done;
    }

    /* Recursively parse a subcommand. */
    if (openparen(token)) {
      did_pass = 1;
      pass_liter = liter;
      pass_delay = delay;

      (*i)--;
      passthru = parse_builtin(input, i, liter, delay);

    } else if (literal(token)) {
      error("esh: parse error: the backslash quote is deprecated.");
      goto done;

    } else if (delaysym(token)) {
      did_pass = 1;
      pass_liter = 1;
      pass_delay = delay+1;

      passthru = parse_builtin(input, i, 1, delay+1);

    } else if (closeparen(token)) {
      break;

    } else if (special(token)) {
      error("esh: weird syntax found while looking for plain text.");
      goto done;
    }



    if (did_pass) {
      if (pass_liter) {
	ls = ls_cons(passthru, ls);

	ls_type_set(ls, TYPE_LIST);

	if (pass_delay) {
	  ls_flag_set(ls, pass_delay);
	}

      } else {
	list* iter;

	if (passthru) {
	  for (iter = passthru; iter != NULL; iter = ls_next(iter)) {

	    if (ls_type(iter) == TYPE_VOID) continue;

	    ls = ls_cons(ls_data(iter), ls);
	    ls_type_set(ls, ls_type(iter));
	    ls_flag_set(ls, ls_flag(iter));
	  }

	  ls_free_shallow(passthru);

	} else {
	  ls = ls_cons(NULL, ls);
	  ls_type_set(ls, TYPE_LIST);
	}
      }
    } else {
      ls = ls_cons(value, ls);
      value = (char*)gc_alloc(sizeof(char) * len, "parse_builtin");
    }
  }

  gc_free(value);

  ls = ls_reverse(ls);
  
  if (liter) {
    return ls;

  } else {
    ret = do_builtin(ls);
    ls_free_all(ls);

    return ret;
  }

 done:
  ls_free_all(ls);
  gc_free(value);
  return NULL;

}


list* parse_sequence(list* ret, char* input, int* i, char* tok) {
  int len = 127;
  char* value = (char*)gc_alloc(sizeof(char) * len,
				"parse_sequence");
  while (1) {
    (*tok) = next_token(input, i, &value, &len);
  
    if (special(*tok)) {
      break;

    } else {
      ret = ls_cons(value, ret);

      value = (char*)gc_alloc(sizeof(char) * len,
			      "parse_sequence");
    }
  }

  gc_free(value);
  
  return ret;
}


list* parse_split(char* input) {
  list* ls = NULL;

  int i = 0;
  char token;

  char* tmp;

  syntax_special = "";

  while (1) {
    ls = parse_sequence(ls, input, &i, &token);
    
    if (!token) break;

    if (special(token)) {
      tmp = (char*)gc_alloc(sizeof(char) * 2, "parse_split");

      tmp[0] = token;
      tmp[1] = '\0';

      ls = ls_cons(tmp, ls);
    }
  }

  syntax_special = NULL;

  return ls_reverse(ls);
}


void parse_pipe(char* input) {
  list* ls = NULL;

  char* f_in = NULL;
  char* f_out = NULL;

  int fd_in = STDIN_FILENO;
  int fd_out = STDOUT_FILENO;

  char token, old, foo = -1;
  int i = 0, bar = 0;
  int len = 128;
  char* value = (char*)gc_alloc(sizeof(char) * len, "parse_pipe");

  list* catter;


  while (1) {
    catter = ls_reverse(parse_sequence(NULL, input, &i, &foo));

    ls = ls_cons(catter, ls);
    ls_type_set(ls, TYPE_LIST);

    if (!separator(foo))
      break;
  }

  old = foo;

  for (bar = 0; bar < 2; bar++) {
    if (old) {
      token = next_token(input, &i, &value, &len);

      if (redirect_in(old) && !special(token) && !f_in) {
	f_in = value;
	foo = -1;
	value = (char*)gc_alloc(sizeof(char) * len, "parse_pipe");

      } else if (redirect_out(old) && !special(token) && !f_out) {
	f_out = value;
	foo = -1;
	value = (char*)gc_alloc(sizeof(char) * len, "parse_pipe");

      } else {
	error("esh: parse error: special syntax where redirection should be.");
	goto done;
      }

      old = next_token(input, &i, &value, &len);
    }
  }

  if (old) {
    error("esh: parse error: extraneous characters after redirection "
	  "operators.");
    goto done;
  }

  ls = ls_reverse(ls);

  if (f_in) {
    fd_in = open_read_aux(f_in);
  }

  if (f_out) {
    fd_out = open_write_aux(f_out);
  }

  /* Mondo-hack: special-case parsing of "list"ed aliases. */

  if (!f_in && !f_out && !ls_next(ls)) {
    list* tmp = ls_data(ls);
    list* alias = hash_get(aliases, ls_data(tmp));

    if (alias && ls_type(alias) == TYPE_LIST) {
      list* oldstack = stack;
      list* tmp2;

      stack = ls_copy(ls_next(tmp));
      tmp2 = eval(alias);
      ls_free_all(stack);
      ls_free_all(tmp2);
      
      stack = oldstack;

      goto done;
    }
  }


  do_pipe(fd_in, fd_out, ls, 0, 0);

  close_aux(fd_in);
  close_aux(fd_out);

 done:

  ls_free_all(ls);
  gc_free(value);

  if (f_in)  gc_free(f_in);
  if (f_out) gc_free(f_out);
}


static void ls_print_aux(list* ls, int i, int delay) {
  list* iter;

  if (i)
    printf("(");

  for (iter = ls; iter != NULL; iter = ls_next(iter)) {

    switch (ls_type(iter)) {
    case TYPE_LIST:
      if (iter && ls_flag(iter) > delay) {
	printf("~");
      }

      ls_print_aux(ls_data(iter), 1, ls_flag(iter));
      break;

    case TYPE_STRING:
      printf("%s", (char*)ls_data(iter));
      break;

    case TYPE_HASH:
      printf("<hash: %p>", ls_data(iter));
      break;

    case TYPE_BOOL:
      printf("<bool: %s>", ls_data(iter) ? "t" : "f");
      break;

    case TYPE_FD:
      {
	int* fd = ls_data(iter);
	printf("<file: %d, %d>", fd[0], fd[1]);
	break;
      }

    case TYPE_PROC:
      printf("<process: %d>", *(pid_t*)(ls_data(iter)));
      break;
    }

    if (ls_next(iter)) {
      printf(" ");
    }
  }

  if (i)
    printf(")");
}


void ls_print(list* ls) {
  ls_print_aux(ls, 0, 0);
}


void parse_command(char* input) {
  int i = 0;
  char token;
  int len = 128;
  char* value = (char*)gc_alloc(sizeof(char) * len, "parse_command");

  syntax_fancy = 1;

  token = next_token(input, &i, &value, &len);

  if (openparen(token)) {
    list* ret;

    syntax_fancy = 0;

    i = 0;
    ret = parse_builtin(input, &i, 0, 0);

    token = next_token(input, &i, &value, &len);

    if (token) {
      error("esh: extraneous characters after command.");

      gc_free(value);
      ls_free_all(ret);
      return;
    }

    if (ret) {
      printf("=>\n");

      ls_print(ret);

      printf("\n");
    }

    ls_free_all(ret);

  } else if (token == '\0') {
    /* Nothing. */

  } else if (special(token)) {
    error("esh: parse error: unexpected special syntax at the beginning of "
	  "the command.");

  } else {
    parse_pipe(input);
  }

  gc_free(value);
}


int parse_file(int file, char** buff, int* len) {
  int i = 0;
  char chr;
  int parencount = 0;
  int did_one = 0;
  int inquote = 0;

  int junk = 0;
  list* ret;

  syntax_fancy = 0;

  while (1) {

    if (read(file, &chr, 1) <= 0) {

      if (parencount || inquote) {
        error("esh: premature end of file while reading a script.");
      }

      return 0;
    }

    if (!inquote && comment(chr)) {
      while (1) {

	if (read(file, &chr, 1) <= 0) {
	  break;
	}

	if (chr == '\n') break;
      }
    }

    if (quote(chr)) {
      if (inquote == quote(chr)) {
	inquote = 0;

      } else if (!inquote) {
	inquote = quote(chr);
      }

    } else if (!inquote && openparen(chr)) {
      parencount++;

    } else if (!inquote && closeparen(chr)) {
      did_one = 1;
      parencount--;

    } 

    if (i >= (*len)-2) {
      char* tmp = (char*)gc_alloc(sizeof(char) * (*len) * 2, "parse_file");

      (*len) *= 2;

      strcpy(tmp, (*buff));
      gc_free((*buff));
      (*buff) = tmp;
    }

    (*buff)[i] = chr;

    i++;

    if (!parencount && did_one) break;
  }

  (*buff)[i] = '\0';

  ret = parse_builtin((*buff), &junk, 0, 0);

  ls_free_all(ret);

  return 1;
}


char* get_prompt(void) {
  char* ret;
  list* prompt_ev;
  
  if (prompt) {
    prompt_ev = eval(prompt);

    ret = ls_strcat(prompt_ev);

    ls_free_all(prompt_ev);

  } else {
    ret = (char*)gc_alloc(sizeof(char) * 3, "get_prompt");
    strcpy(ret, "$ ");
  }

  return ret;
}


void do_file(char* fname, int do_error) {
  int file = STDIN_FILENO;

  int len = 256;
  char* buff = (char*)gc_alloc(sizeof(char) * len, "do_file");

  if (fname) {
    file = open_read_error_aux(fname);
  }

  if (file < 0) {
    if (do_error) {
      if (fname) {
	error("esh: could not load script file \"%s\".", fname);
      } else {
	error("esh: could not load script file from stdin.");
      }
    }

    gc_free(buff);
    return;
  }

  while (parse_file(file, &buff, &len)) {
    arrange_funeral();
  }

  close_aux(file);

  gc_free(buff);
}
  


static void exception(int signum) {
  exception_flag = 1;

  signal(SIGINT, exception);
}


void init_shell(int argc, char** argv) {
  char buff[256];
  char* homedir;
  int i;

  pid_t pgid, self_pid;

  int* tmp1;
  int* tmp2;

  tmp1 = (int*)gc_alloc(sizeof(int) * 2, "init_shell");
  tmp2 = (int*)gc_alloc(sizeof(int) * 2, "init_shell");

  builtins = (hash_table*)gc_alloc(sizeof(hash_table), "init_shell");
  aliases = (hash_table*)gc_alloc(sizeof(hash_table), "init_shell");
  defines = (hash_table*)gc_alloc(sizeof(hash_table), "init_shell");

  hash_init(builtins, builtins_array);
  hash_init(aliases, NULL);
  hash_init(defines, NULL);

  interactive = isatty(shell_terminal_fd);

  ls_true = ls_cons((void*)1, NULL);
  ls_type_set(ls_true, TYPE_BOOL);

  ls_false = ls_cons((void*)0, NULL);
  ls_type_set(ls_false, TYPE_BOOL);

  tmp1[0] = STDIN_FILENO;
  tmp1[1] = STDOUT_FILENO;

  tmp2[0] = STDIN_FILENO;
  tmp2[1] = STDERR_FILENO;

  ls_stdio = ls_cons(tmp1, NULL);
  ls_type_set(ls_stdio, TYPE_FD);

  ls_stderr = ls_cons(tmp2, NULL);
  ls_type_set(ls_stderr, TYPE_FD);

  ls_void = ls_cons(NULL, NULL);
  ls_type_set(ls_void, TYPE_VOID);

  if (interactive) {
  
    while (1) {
      pgid = getpgrp();
    
      if (tcgetpgrp(shell_terminal_fd) == pgid) break;

      kill(-pgid, SIGTTIN);
    }

    /* Ignore interactive and job-control signals.  */
    signal(SIGINT, exception);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    
    signal(SIGCHLD, babysit);

    self_pid = getpid();

    if (setpgid(0, self_pid) < 0) {
      error("esh: could not put myself in my own process group.");
      error("esh: %s.", 
            (errno == EINVAL ? "pgid < 0" :
            (errno == EPERM  ? "permission denied" :
            (errno == ESRCH  ? "no such PID" : 
            ("unknown error")))));             
    }
    
    tcgetattr(shell_terminal_fd, &shell_terminal_modes);

    read_init();
  }

  for (i = argc-1; i > 0; i--) {
    stack = ls_cons(dynamic_strcpy(argv[i]), stack);
  }

  register_chdir();

  do_file("/etc/eshrc", 0);

  homedir = getenv("HOME");

  if (homedir) {
    strncpy(buff, homedir, 255);
    strncat(buff, "/.eshrc", 255);

    buff[255] = '\0';

    do_file(buff, 0);
  }
}


int main(int argc, char** argv, char** env) {
  char* pmt;
  char* line = NULL;
  list* tmp = NULL;

  environ = env;
  init_shell(argc, argv);

  if (interactive) {
    while (1) {
      exception_flag = 0;
      arrange_funeral();
    
      pmt = get_prompt();

      line = read_read(pmt);

      gc_free(pmt);

      if (!line) break;

      parse_command(line);

      gc_free(line);
    }

  } else {
    do_file(NULL, 1);
  }

  
  close_aux(stderr_handler_fd);

#ifdef MEM_DEBUG

  for (tmp = jobs; tmp != NULL; tmp = ls_next(tmp)) {
    gc_free(((job_t*)ls_data(tmp))->name);
  }

  ls_free_all(jobs);

  jobs = NULL;

  ls_free_all(ls_void);
  ls_free_all(ls_true);
  ls_free_all(ls_false);
  ls_free_all(ls_stdio);
  ls_free_all(ls_stderr);
  ls_free_all(prompt);
  ls_free_all(stack);

  hash_free(aliases, ls_free_all);
  hash_free(defines, ls_free_all);
  hash_free(builtins, NULL);

  gc_free(aliases);
  gc_free(defines);
  gc_free(builtins);

  if (syntax_blank) {
    gc_free(syntax_blank);
  }

  if (syntax_special) {
    gc_free(syntax_special);
  }

  read_done();

  gc_diagnostics();

#endif

  return EXIT_SUCCESS;
}

