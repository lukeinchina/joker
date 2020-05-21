CC=gcc
CFLAGS=-g -Wall -Wextra

#ficial Neural Networks
OBJS= httpd.o util.o
all: httpd

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)


httpd: $(OBJS) 
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -f *.o  httpd *.h.gch
