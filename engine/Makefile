CC=gcc
CXX=g++
CFLAGS=-W -Wall -pipe -fPIC -g

REDISSO=redis.so
MEMCACHEDSO=memcached.so

.PHONY: all
all: $(REDISSO) $(MEMCACHEDSO)
	@echo "make so done"

.PHONY: dependencies
dependencies:
	 @cd ../deps/hiredis && make

REDISSOOBJ=redis.o
MEMCACHEDOBJ=memcached.o rawAsyncClient.o 

redis.o: redis.c

redis.o:
	$(CC) -c $(CFLAGS) $< -I .. -I../deps/hiredis -I../deps/libev

redis.so: $(REDISSOOBJ) dependencies
	$(CC) -shared $(CFLAGS) -o $(REDISSO) $(REDISSOOBJ) ../deps/hiredis/libhiredis.a

memcached.o: memcached.c rawAsyncClient.h
	$(CC) -c $(CFLAGS) $< -I .. -I../deps/libev

rawAsyncClient.o: rawAsyncClient.c
	$(CC) -c $(CFLAGS) $< -I .. -I../deps/libev

$(MEMCACHEDSO): $(MEMCACHEDOBJ)
	$(CC) -shared $(CFLAGS) -o $(MEMCACHEDSO) $(MEMCACHEDOBJ)

.PHONY: clean
clean:
	rm -rf *.o $(REDISSO) $(MEMCACHEDSO)
	@cd ../deps/hiredis && make clean
