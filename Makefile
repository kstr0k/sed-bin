# Only use this Makefile from its own folder or via make -C .../sed-bin

objs = address.o operations.o read.o sed-bin.o
BIN ?= sed-bin  # must be simple basename, no spaces

all: $(BIN)

$(BIN): $(objs)
	@# the line below is implicit with GNU make, add it for BSD compatibility
	$(CC) $(objs) -o $@

sed-bin.o: generated.c generated-init.c

.PHONY: clean all

clean:
	rm -f *.o $(BIN)
