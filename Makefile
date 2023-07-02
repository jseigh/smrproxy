#  Makefile template for Static library. 
# 1. Compile every *.c in the folder 
# 2. All obj files under obj folder
# 3. static library .a at lib folder
# 4. run 'make dirmake' before calling 'make'


CC = gcc
CFLAGS = -O3 -std=gnu17 -ggdb
INC = -I include

E = 
S = $E $E


SRCFILES1 = $(wildcard src/*.c)
OBJFILES1 = $(patsubst src/%.c, lib/%.o, $(SRCFILES1))

SRCFILES2 = $(wildcard src/platform/linux/*.c)
OBJFILES2 = $(patsubst src/platform/linux/%.c, lib/%.o, $(SRCFILES2))

SRCFILES = $(SRCFILES1) $(SRCFILES2)
OBJFILES = $(OBJFILES1) $(OBJFILES2)

OUTFILE = lib/libsmrproxy.a


# Default target
.PHONY: all
all: $(OUTFILE)

#Compiling every *.c to *.o
#lib/$%.o: src/$%.c
$(OBJFILES): $(SRCFILES)
	set -x; for s in $^; do $(CC) -c $(INC) $(CFLAGS) -o lib/$$(basename $$s .c).o  $$s; done
	


$(OUTFILE): $(OBJFILES)
	ar ru $(OUTFILE) $(OBJFILES)
#	ranlib $@

		
#.PHONY: clean	
clean:
	echo MAKE_HOST $(MAKE_HOST)
	rm -f lib/*.o
	rm -f $(OUTFILE)
	rm -rf html
	rm -rf latex

rebuild: clean build


 
