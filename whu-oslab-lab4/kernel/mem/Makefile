include ../../common.mk

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

.PHONY: build clean

build: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -I ../../include -c $< -o $@

clean:
	rm -f *.o *.d
