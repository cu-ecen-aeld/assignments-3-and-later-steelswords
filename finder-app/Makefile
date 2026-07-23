CC="${CROSS_COMPILE}gcc"
CXX="${CROSS_COMPILE}g++"
LINKER="${CROSS_COMPILE}gcc"

writer: writer.c
	$(CC) -o $@ $^

.phony: clean
clean:
	@rm writer

