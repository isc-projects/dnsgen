CXXFLAGS	= -O3 -g -std=c++11 -Wall -Werror
LDFLAGS		= -lpthread -lresolv

TARGETS		= dnsgen dnsecho

all:		$(TARGETS)

dnsgen:		main.o datafile.o query.o
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

dnsecho:	echo.o
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

datafile.o:	ring.h query.h

query.o:	query.h

clean:
	$(RM) $(TARGETS) *.o
