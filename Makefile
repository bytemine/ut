#
#	Makefile for 'ut'
#	(channel based service multiplexer)
#

VERSION=2.0

# internal UT version string
UT_VERSION=0.5
#UT_VERSION=$(VERSION)

# default to /usr/local/, but keep it overridable
PREFIX?=/usr/local/
OWNER=root
GROUP=wheel

# -------

# path to config file
DEFS+= -DUT_CONFIG_PATH=\"/etc/btm/ut.conf\"

# keepalive interval (in seconds)
DEFS+= -DUT_KEEPALIVE=30

# keepalive string
DEFS+= -DUT_KASTRING=\"___PING___\"

# -------

# debugging flags
DEBUG= -DDEBUG -g

# warning flags
WARN= -Wall -W #-std=c99 -pedantic

CFLAGS+= $(WARN) $(DEFS) -DUT_VERSION=\"$(UT_VERSION)\"

# keep compiler quiet for flex generated source
# (disable at least once if .l file changes)
FLEXNOWARN=-Wno-unused-function -Wno-unused-label
#

OBJS=	main.o		\
	conf.o		\
	tesc.o		\
	data.o		\
	mlpx.o		\
	cmdi.o		\
	util.o


# --------------------------------------------

all: ut

ut: $(OBJS)
	$(CC) $(OBJS) -o $@

usrv: usrv.o
	$(CC) usrv.o -o $@

clean:
	rm -f ut $(OBJS) conf.c usrv usrv.o

install: ut
	install -d -o $(OWNER) -g $(GROUP) -m 0755 $(PREFIX)/sbin
	install -s -o $(OWNER) -g $(GROUP) -m 0755 ut $(PREFIX)/sbin/
	install -d -o $(OWNER) -g $(GROUP) -m 0755 $(PREFIX)/man/man5/
	install -d -o $(OWNER) -g $(GROUP) -m 0755 $(PREFIX)/man/man8/
	install -o $(OWNER) -g $(GROUP) -m 0755 ut.8 $(PREFIX)/man/man8/
	install -o $(OWNER) -g $(GROUP) -m 0755 ut.conf.5 $(PREFIX)/man/man5/

distfile: clean
	rm -rf /tmp/socket-wrapper-$(VERSION)
	cp -R ../socket-wrapper /tmp/socket-wrapper-$(VERSION)
	rm /tmp/socket-wrapper-$(VERSION)/.gitignore
	cd /tmp && tar czfv /tmp/socket-wrapper-$(VERSION).tar.gz \
		socket-wrapper-$(VERSION)/

# flex generated source
conf.c: conf.l
	flex -o$@ conf.l

# turn of warnings for unused functions in flex generated source
conf.o: conf.c
	$(CC) $(CFLAGS) $(FLEXNOWARN) -c conf.c

# --------------------------------------------

conf.c: Makefile

$(OBJS): Makefile

main.o: main.c conf.h mlpx.h data.h tesc.h
conf.o: conf.c conf.h mlpx.h data.h tesc.h
tesc.o: tesc.c conf.h mlpx.h data.h tesc.h cmdi.h
data.o: data.c conf.h mlpx.h data.h tesc.h
mlpx.o: mlpx.c conf.h mlpx.h data.h tesc.h cmdi.h util.h
cmdi.o: cmdi.c conf.h mlpx.h data.h tesc.h util.h
util.o: util.c

### end ###
