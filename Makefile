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


SRCFILES = $(wildcard src/*.c)
OBJFILES = $(patsubst src/%.c, lib/%.o, $(SRCFILES))
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


 
