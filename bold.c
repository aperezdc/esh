/*
 * esh, the Unix shell with Lisp-like syntax.
 * Copyright (C) 1999  Ivan Tkatchev
 * This source code is under the GPL.
 */


#include <stdlib.h>
#include <stdio.h>

char colorspec[10];

char* colors[] = {
  "black",   "30",
  "red",     "31",
  "green",   "32",
  "yellow",  "33",
  "blue",    "34",
  "magenta", "35",
  "cyan",    "36",
  "white",   "37",
  NULL, NULL
};

char* flags[] = {
  "none",       "00",
  "bold",       "01",
  "underscore", "04",
  "blink",      "05",
  "reverse",    "07",
  "concealed",  "08",
  NULL, NULL
};

int main(int argc, char** argv) {
  char* ss = "\033[%s;%sm";
  char* flag = NULL;
  char* color = NULL;
  char* se = "\033[00m";
  int chr;
  int i = 0;

  if (argc != 3) {
    printf("Usage: %s <flag> <color>\n"
	   "Where <color> is one of:\n"
	   "black red green yellow blue magenta cyan white\n"
	   "And <flag> is one of:\n"
	   "none bold underscore blink reverse concealed\n",
	   argv[0]);
    exit(EXIT_FAILURE);
  }

  while (flags[i]) {
    if (strcmp(flags[i], argv[1]) == 0) {
      flag = flags[i+1];
    }

    i += 2;
  }

  i = 0;

  while (colors[i]) {
    if (strcmp(colors[i], argv[2]) == 0) {
      color = colors[i+1];
    }

    i += 2;
  }

  if (!flag || !color) {
    fprintf(stderr, "bold: either color or flag argument is wrong.\n");
    return EXIT_FAILURE;
  }

  sprintf(colorspec, ss, flag, color);


  while (1) {
    chr = getchar();

    if (chr == EOF) break;

    printf("%s%c%s", colorspec, chr, se);
    fflush(stdout);
  }

  return EXIT_SUCCESS;
}
