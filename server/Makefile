CC="${CROSS_COMPILE}gcc"
CXX="${CROSS_COMPILE}g++"
LINKER="${CROSS_COMPILE}gcc"
LDFLAGS := -pthread $(LDFLAGS)
CCFLAGS := -I. $(CCFLAGS)

OBJDIR := obj

all: aesdsocket


SRCS := $(shell find $(SRC_DIRS) -name '*.c')

OBJS := $(patsubst %.c,$(OBJDIR)/%.o, $(SRCS))

$(OBJDIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -c $@ $<

signal_handle.o: signal_handle.c signal_handle.h
	mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -c $@ $^

aesdsocket: $(OBJS)
	shell echo These are the object files: $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $(CCFLAGS) $^

.phony: clean
clean:
	-rm aesdsocket
	-rm *.o
	-rm -r $(OBJDIR)
