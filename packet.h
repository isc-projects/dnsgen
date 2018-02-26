#ifndef __packet_h
#define __packet_h

#include <string>
#include <linux/if_packet.h>

class PacketSocket {

public:
	int		fd = -1;

public:
			~PacketSocket();

public:
	void		open();
	void		close();

	void		bind(unsigned int ifindex);
	void		bind(const std::string& ifname);

	int		setopt(int optname, const uint32_t val);
	int		getopt(int optname, uint32_t& val);

	void*		rx_ring(const tpacket_req& req);
};

#endif // __packet_h
