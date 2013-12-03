CC=gcc
CPP=g++
CFLAGS=-Wall -W -g -pipe -fPIC

DMONITORNAME=dmonitor
ANALYZERNAME=analyzer
ENGINESO=engineso

all: $(DMONITORNAME) $(ANALYZERNAME) $(ENGINESO)
	@rm -rf output
	@mkdir -p output && cd output && mkdir dmonitor && cd dmonitor && mkdir -p lib && mkdir -p log && mkdir -p bin && mkdir -p conf \
	    && cd .. && mkdir analyzer && cd analyzer && mkdir -p lib && mkdir -p log && mkdir -p bin && mkdir -p conf 
	@cp dmonitor output/dmonitor/bin && cp engine/*.so output/dmonitor/lib && touch output/dmonitor/log/.log && cp analyzer output/analyzer/bin && cp engine/*.so output/analyzer/lib && touch output/analyzer/log/.log
	@cp -r env/* output
	@mkdir -p output/analyzer/scripts && cp -r scripts/* output/analyzer/scripts
	@echo "Done!"

DMONITOROBJ=dmonitor.o sds.o config.o log.o meta.o connection.o monitor.o store.o action.o
ANALYZEROBJ=analyzer.o sds.o config.o log.o meta.o connection.o gather.o store.o action.o

#deps
connection.o: connection.c connection.h log.h deps/libev/ev.h
meta.o: meta.c meta.h log.h deps/libev/ev.h
log.o: log.c log.h
sds.o: sds.c sds.h
config.o: config.c config.h sds.h log.h deps/libev/ev.h
monitor.o: monitor.c monitor.h log.h deps/libev/ev.h
gather.o: gather.c gather.h log.h deps/libev/ev.h
store.o: store.c store.h log.h deps/hiredis/async.h
action.o: action.c action.h log.h deps/libev/ev.h
dmonitor.o: dmonitor.c dmonitor.h log.h log.c config.h meta.h connection.h uthash.h deps/libev/ev.h 
analyzer.o: analyzer.c analyzer.h log.h log.c config.h meta.h connection.h uthash.h deps/libev/ev.h 

$(DMONITORNAME): dependencies $(DMONITOROBJ)
$(ANALYZERNAME): dependencies $(ANALYZEROBJ)

.PHONY: dependencies
dependencies: 
	@cd deps/libev && ./configure && make
	@cd deps/hiredis && make

.PHONY: $(ENGINESO)
engineso:
	@cd engine && make

#cc
#dmonitor
log.o:
	$(CC) -c $(CFLAGS) $<
sds.o:
	$(CC) -c $(CFLAGS) $<
config.o: 
	$(CC) -c $(CFLAGS) $< -I deps/libev
connection.o: 
	$(CC) -c $(CFLAGS) $< -I deps/libev
meta.o: 
	$(CC) -c $(CFLAGS) $< -I deps/libev -I deps/hiredis
monitor.o:
	$(CC) -c $(CFLAGS) $< -I deps/libev
dmonitor.o:
	$(CC) -c $(CFLAGS) $< -I deps/libev
store.o:
	$(CC) -c $(CFLAGS) $< -I deps/libev -I deps/hiredis
gather.o:
	$(CC) -c $(CFLAGS) $< -I deps/libev -I deps/hiredis
action.o:
	$(CC) -c $(CFLAGS) $< -I deps/libev -I deps/hiredis
analyzer.o:
	$(CC) -c $(CFLAGS) $< -I deps/libev
dmonitor: 
	$(CC) -o $(DMONITORNAME) $(DMONITOROBJ) deps/libev/.libs/libev.a deps/hiredis/libhiredis.a -lm -ldl -lpthread -rdynamic 
analyzer: 
	$(CC) -o $(ANALYZERNAME) $(ANALYZEROBJ) deps/libev/.libs/libev.a deps/hiredis/libhiredis.a -lm -ldl -lpthread -rdynamic 

.PHONY: clean
clean:
	rm -rf *.o $(DMONITORNAME)
	@cd engine && make clean
	rm -rf output

.PHONY: install
install:
	@scp -r output/dmonitor/bin/dmonitor chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor1/bin
	@scp -r output/dmonitor/bin/dmonitor chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor2/bin
	@scp -r output/dmonitor/bin/dmonitor chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor3/bin
	@scp -r output/dmonitor/bin/dmonitor chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor4/bin
	@scp -r output/dmonitor/bin/dmonitor chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor5/bin
	@scp -r output/dmonitor/lib chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor1
	@scp -r output/dmonitor/lib chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor2
	@scp -r output/dmonitor/lib chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor3
	@scp -r output/dmonitor/lib chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor4
	@scp -r output/dmonitor/lib chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/dmonitor5
	@cd output/analyzer && scp -r bin conf lib scripts supervise.* chendazhuang@db-rdqa-pool003.db01.baidu.com:dzch/analyzer
