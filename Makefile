#
# Flags to the compiler:
#
#   MEM_DEBUG          Check for memory leaks.
#
#DEFINES += MEM_DEBUG

# Where your readline library is.
# You can compile with a "gets()" replacement instead.

#READ      = read-stdio
READ     ?= read-rl

INCLUDES += /usr/include/readline
LIB      += -lncurses -lreadline

# No need to change things from this point onwards

CFLAGS   += -Wall
DEFINES  := $(patsubst %,-D%,$(DEFINES))
INCLUDES := $(patsubst %,-I%,$(INCLUDES))
CPPFLAGS += $(DEFINES) $(INCLUDES)

OBJS := list.o hash.o builtins.o esh.o format.o gc.o $(READ).o
VERS := 0.8.5

all: esh

bold: bold.o

esh: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LIB) -o esh

clean:
	$(RM) $(OBJS) bold.o esh bold

dist:
	git archive --prefix=esh-$(VERS)/ v$(VERS) | xz -9c > esh-$(VERS).tar.xz

.PHONY: dist

depend:
	makedepend -Y $(OBJS:.o=.c) read*.c

.PHONY: depend

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
