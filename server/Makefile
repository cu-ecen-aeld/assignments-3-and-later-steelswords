ifdef $(CROSS_COMPILE)
	CC := $(CROSS_COMPILE)gcc
	CXX := $(CROSS_COMPILE)g++
	LINKER := $(CROSS_COMPILE)gcc
else
	CC ?= gcc
	CXX ?= g++
	LINKER ?= gcc
endif

LDFLAGS ?= -pthread
CFLAGS ?= -g -I. -Wall -Werror -Wpedantic

OBJDIR := obj

all: aesdsocket


SRCS := $(shell find $(SRC_DIRS) -name '*.c')

OBJS := $(patsubst %.c,$(OBJDIR)/%.o, $(SRCS))

$(OBJDIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

signal_handle.o: signal_handle.c signal_handle.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $^

aesdsocket: $(OBJS)
	$(LINKER) -o $@ $(LDFLAGS) $(CFLAGS) $^

.phony: clean
clean:
	-rm aesdsocket
	-rm *.o
	-rm -r $(OBJDIR)
