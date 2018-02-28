CXXFLAGS	= -O3 -g -std=c++11 -Wall -Werror
LDFLAGS		= -lpthread -lresolv

TARGETS		= dnsgen dnsecho

COMMON_OBJS	= packet.o util.o

all:		$(TARGETS)

dnsgen:		dnsgen.o datafile.o query.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

dnsecho:	dnsecho.o packet.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

datafile.o:	ring.h query.h

query.o:	query.h

packet.o:	packet.h

clean:
	$(RM) $(TARGETS) *.o
