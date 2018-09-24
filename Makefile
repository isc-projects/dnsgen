CXXFLAGS	= -O3 -std=c++11 -Wall -Werror

LDFLAGS		=

LIBS_DNS	= -lresolv
LIBS_THREAD	= -lpthread

TARGETS		= dnsgen dnsecho dnscvt

COMMON_OBJS	= util.o

all:		$(TARGETS)

dnsgen:		dnsgen.o packet.o queryfile.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS_DNS) $(LIBS_THREAD)

dnsecho:	dnsecho.o packet.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS_THREAD)

dnscvt:		dnscvt.o queryfile.o $(COMMON_OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS_DNS) $(LIBS_THREAD)

clean:
	$(RM) $(TARGETS) *.o

queryfile.o:	queryfile.h

packet.o:	packet.h

util.o:		util.h
