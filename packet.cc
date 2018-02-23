#include <iostream>
#include <stdexcept>
#include <cerrno>

#include <unistd.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "packet.h"
#include "util.h"

int socket_open(int ifindex)
{
	int fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	if (fd < 0) {
		throw_errno("socket AF_PACKET");
	}

	// bind the AF_PACKET socket to the specified interface
	sockaddr_ll saddr = { 0, };
	saddr.sll_family = AF_PACKET;
	saddr.sll_ifindex = ifindex;

	if (bind(fd, reinterpret_cast<sockaddr *>(&saddr), sizeof(saddr)) < 0) {
		throw_errno("bind AF_PACKET");
	}

	// set the AF_PACKET socket's fanout mode
	uint32_t fanout = (getpid() & 0xffff) | (PACKET_FANOUT_CPU << 16);
	if (socket_setopt(fd, PACKET_FANOUT, fanout) < 0) {
		throw_errno("setsockopt PACKET_FANOUT");
	}

	return fd;
}

int socket_setopt(int fd, int name, const uint32_t val)
{
	return setsockopt(fd, SOL_PACKET, name, &val, sizeof val);
}

int socket_getopt(int fd, int name, uint32_t& val)
{
	socklen_t len = sizeof(val);
	return getsockopt(fd, SOL_PACKET, name, &val, &len);
}
