#ifndef __packet_h
#define __packet_h

#include <string>
#include <poll.h>
#include <linux/if_packet.h>

class PacketSocket {

private:
	pollfd		pfd;

public:
	int		fd = -1;

public:
			~PacketSocket();

public:
	void		open();
	void		close();

	void		bind(unsigned int ifindex);
	void		bind(const std::string& ifname);
	int		poll(int timeout = -1);

	int		setopt(int optname, const uint32_t val);
	int		getopt(int optname, uint32_t& val);

	void*		rx_ring(const tpacket_req& req);
};

#endif // __packet_h
