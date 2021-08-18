# Only use this Makefile from its own folder or via make -C .../sed-bin

objs = address.o operations.o read.o sed-bin.o
BIN ?= sed-bin  # must be simple shell word, no spaces
SED_TRANSLATOR ?= par.sed  # like BIN
SED_TRANSLATOR_SED ?= sed -f

all: $(BIN)

$(BIN): $(objs)
	@# the line below is implicit with GNU make, add it for BSD compatibility
	$(CC) $(objs) -o $@

sed-bin.o: generated.c generated-init.c

generated.c: generated-init.c

# This would force the user.sed build path; instead: 'generate' phony target
#generated-init.c: user.sed $(SED_TRANSLATOR)
generate: user.sed $(SED_TRANSLATOR)
	$(SED_TRANSLATOR_SED) $(SED_TRANSLATOR) <user.sed >generated.c

.PHONY: clean all

clean:
	rm -f *.o $(BIN) generated-init.c generated.c user.sed
