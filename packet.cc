#include <iostream>
#include <stdexcept>
#include <algorithm>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if.h>

#include "packet.h"
#include "util.h"

PacketSocket::~PacketSocket()
{
	if (map) {
		::munmap(map, req.tp_frame_size * req.tp_frame_nr);
		map = nullptr;
	}

	if (fd >= 0) {
		::close(fd);
	}
}

void PacketSocket::open()
{
	fd = ::socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	if (fd < 0) {
		throw_errno("socket(AF_PACKET, SOCK_DGRAM)");
	}

	pfd = { fd, POLLIN, 0 };
}

void PacketSocket::close()
{
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}
}

int PacketSocket::setopt(int name, const uint32_t val)
{
	return ::setsockopt(fd, SOL_PACKET, name, &val, sizeof val);
}

int PacketSocket::getopt(int name, uint32_t& val)
{
	socklen_t len = sizeof(val);
	return ::getsockopt(fd, SOL_PACKET, name, &val, &len);
}

void PacketSocket::bind(unsigned int ifindex)
{
	// bind the AF_PACKET socket to the specified interface
	sockaddr_ll saddr = { 0, };
	saddr.sll_family = AF_PACKET;
	saddr.sll_ifindex = ifindex;

	if (::bind(fd, reinterpret_cast<sockaddr *>(&saddr), sizeof(saddr)) < 0) {
		throw_errno("bind AF_PACKET");
	}

	// set the AF_PACKET socket's fanout mode
	uint32_t fanout = (getpid() & 0xffff) | (PACKET_FANOUT_CPU << 16);
	if (setopt(PACKET_FANOUT, fanout) < 0) {
		throw_errno("setsockopt PACKET_FANOUT");
	}
}

void PacketSocket::bind(const std::string& ifname)
{
	unsigned int index = if_nametoindex(ifname.c_str());
	if (index == 0) {
		throw_errno("if_nametoindex");
	}
	bind(index);
}

int PacketSocket::poll(int timeout)
{
	int res = ::poll(&pfd, 1, timeout);
	if (res < 0) {
		throw_errno("poll");
	}

	return res;
}

void PacketSocket::rx_ring_enable(size_t frame_bits, size_t frame_nr)
{
	size_t page_size = sysconf(_SC_PAGESIZE);

	req.tp_frame_nr = frame_nr;
	req.tp_frame_size = (1 << frame_bits);

	size_t map_size = req.tp_frame_size * req.tp_frame_nr;

	req.tp_block_size = std::max(page_size, size_t(req.tp_frame_size));
	req.tp_block_nr = map_size / req.tp_block_size;

	if (setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req)) < 0) {
		throw_errno("PacketSocket::rx_ring_enable(PACKET_RX_RING)");
	}

	void *p = ::mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
	if (p == MAP_FAILED) {
		throw_errno("mmap");
	}

	map = reinterpret_cast<uint8_t*>(p);

	ll_offset = TPACKET_ALIGN(sizeof(struct tpacket_hdr));
}

void PacketSocket::rx_ring_next(PacketSocket::rx_callback_t callback, void *userdata)
{
	auto frame = map + rx_current * req.tp_frame_size;
	auto& hdr = *reinterpret_cast<tpacket_hdr*>(frame);

	if ((hdr.tp_status & TP_STATUS_USER) == 0) {
		if (poll() == 0) return;
	}

	auto client = reinterpret_cast<sockaddr_ll *>(frame + ll_offset);
	auto buf = frame + hdr.tp_net;

	callback(buf, hdr.tp_len, client, userdata);

	hdr.tp_status = TP_STATUS_KERNEL;
	rx_current = (rx_current + 1) % req.tp_frame_nr;
}
