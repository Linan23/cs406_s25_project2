.PHONY: all clean

CC       = gcc
CFLAGS   = -Wall -Wextra -pthread
LDFLAGS  = -pthread

# Object files for each program
OBJS     = wserver.o request.o io_helper.o
COBJS    = wclient.o io_helper.o
SQL_OBJS = sql.o blockio.o io_helper.o

.SUFFIXES: .c .o

# Build all programs
all: wserver wclient spin.cgi sql.cgi

# Run the smoke tests after building
test: all
	bash ./test_server.sh
wserver: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

wclient: $(COBJS)
	$(CC) $(CFLAGS) -o $@ $(COBJS)

spin.cgi: spin.c
	$(CC) $(CFLAGS) -o $@ spin.c

sql.cgi: $(SQL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SQL_OBJS)

# Generic rule to compile .c into .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f *.o wserver wclient spin.cgi sql.cgi schema.db movies.data
