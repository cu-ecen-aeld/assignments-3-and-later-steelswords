CC="${CROSS_COMPILE}gcc"
CXX="${CROSS_COMPILE}g++"
LINKER="${CROSS_COMPILE}gcc"
LDFLAGS := -pthread $(LDFLAGS)
CFLAGS := -g -I. $(CFLAGS) -Wall -Werror -Wpedantic

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
	$(CC) -o $@ $(LDFLAGS) $(CFLAGS) $^

.phony: clean
clean:
	-rm aesdsocket
	-rm *.o
	-rm -r $(OBJDIR)
