
# Your C compiler.

CC=gcc

# Where your readline library is.
# You can compile with a "gets()" replacement instead.

#INC=
#LIB=
#READ=read-stdio

INC=-I/usr/include/readline
LIB=-lreadline -ltermcap
READ=read-rl

# Flags to the compiler: 
#
# -DMEM_DEBUG          Check for memory leaks.
#

CFLAGS=-g -Wall -DMEM_DEBUG $(INC)

# No need to change this stuff.

OBJS=list.o hash.o builtins.o esh.o format.o gc.o $(READ).o
VERS=0.8.5

all: $(OBJS)
	$(CC) $(OBJS) $(LIB) -o esh

backup:
	cp -f Makefile *.[ch] /home/backup/esh

clean:
	-rm *~ */*~ *.o core bold esh gmon.out

dist:
	-rm esh*tar.gz
	cd ..; tar -c esh/* > esh/esh-$(VERS).tar
	gzip esh-$(VERS).tar

depend:
	makedepend -Y $(OBJS:.o=.c) read*.c


# DO NOT DELETE

list.o: gc.h list.h hash.h
hash.o: gc.h list.h hash.h
builtins.o: common.h format.h list.h gc.h hash.h job.h esh.h builtins.h
builtins.o: read.h
esh.o: common.h format.h list.h gc.h hash.h job.h builtins.h read.h
gc.o: gc.h format.h
read-stdio.o: common.h gc.h list.h hash.h read.h
read-rl.o: common.h gc.h list.h hash.h read.h
read-stdio.o: common.h gc.h list.h hash.h read.h
