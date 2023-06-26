CC = gcc
CFLAGS = -O3 -std=gnu17 -ggdb
INC = -I ../include
LDFLAGS = -L ../lib -l smrproxy

#Compiling every *.cpp to *.o
smrtest: smrtest.c smrtestconfig.c
	$(CC) $(CFLAGS) $(INC) smrtest.c smrtestconfig.c  $(LDFLAGS) -o smrtest
	$(CC) $(CFLAGS) -D SMRPROXY_MB $(INC) smrtest.c smrtestconfig.c  $(LDFLAGS) -o smrtest_mb
	$(CC) $(CFLAGS) $(INC) -S reftest.c
	$(CC) $(CFLAGS) $(INC) example.c $(LDFLAGS) -o example


clean:
	rm -f smrtest
	rm -f smrtest_mb
	rm -f example
	rm -f *.o
	rm -f reftest.s
	rm -rf html
	rm -rf latex
