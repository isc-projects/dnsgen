CXXFLAGS	= -O0 -g -std=c++11 -Wall -Werror

LDFLAGS		=

LIBS_DNS	= -lresolv
LIBS_THREAD	= -lpthread
LIBS_CURSES	= -lncurses++ -lncurses

TARGETS		= dnsgen dnsecho dnscvt ethq

COMMON_OBJS	= packet.o timespec.o util.o

all:		$(TARGETS)

dnsgen:		dnsgen.o datafile.o query.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS_DNS) $(LIBS_THREAD)

dnsecho:	dnsecho.o packet.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS_THREAD)

dnscvt:		dnscvt.o datafile.o query.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS_DNS) $(LIBS_THREAD)

ethq:		ethq.o ethtool++.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS_CURSES)

clean:
	$(RM) $(TARGETS) *.o

datafile.o:	datafile.h query.h

ethtool++.o:	ethtool++.h

packet.o:	packet.h

query.o:	query.h

timespec.o:	timespec.h

util.o:		util.h
