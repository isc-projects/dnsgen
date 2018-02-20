#include <iostream>
#include <stdexcept>
#include <cerrno>

#include <unistd.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

int get_socket(int ifindex)
{
	int fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	if (fd < 0) {
		throw std::system_error(errno, std::system_category(), "socket AF_PACKET");
	}

	// bind the AF_PACKET socket to the specified interface
	sockaddr_ll saddr = { 0, };
	saddr.sll_family = AF_PACKET;
	saddr.sll_ifindex = ifindex;

	if (bind(fd, reinterpret_cast<sockaddr *>(&saddr), sizeof(saddr)) < 0) {
		throw std::system_error(errno, std::system_category(), "bind AF_PACKET");
	}

	// set the AF_PACKET socket's fanout mode
	uint32_t fanout = (getpid() & 0xffff) | (PACKET_FANOUT_CPU << 16);
	if (setsockopt(fd, SOL_PACKET, PACKET_FANOUT, &fanout, sizeof fanout) < 0) {
		throw std::system_error(errno, std::system_category(), "setsockopt PACKET_FANOUT");
	}

	return fd;
}
