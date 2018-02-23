#include <iostream>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <thread>

#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <linux/version.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "packet.h"
#include "util.h"

typedef struct {
	int				index;
	int				fd;
	tpacket_req			tp;
	uint8_t*			map;
} thread_data_t;

typedef struct {
	uint16_t			ifindex;
	uint16_t			dest_port;
	in_addr_t			src_ip;
	in_addr_t			dest_ip;
} global_data_t;

ssize_t do_echo(global_data_t& gd, thread_data_t& td,
		uint8_t *buffer, size_t len,
		const sockaddr_ll *client, socklen_t clientlen)
{
	auto& ip = *reinterpret_cast<iphdr *>(buffer);
	auto& udp = *reinterpret_cast<udphdr *>(buffer + 4 * ip.ihl);

	if (udp.dest != htons(gd.dest_port)) {
		return 0;
	}

	std::swap(ip.saddr, ip.daddr);
	std::swap(udp.source, udp.dest);

	auto res = sendto(td.fd, buffer, len, MSG_DONTWAIT, reinterpret_cast<const sockaddr *>(client), clientlen);
	if (res < 0 && errno != EAGAIN) {
		throw_errno("sendto");
	}

	return res;
}

ssize_t echo_one(global_data_t& gd, thread_data_t& td)
{
	uint8_t buffer[4096];
	sockaddr_ll client;
	socklen_t clientlen = sizeof(client);

	auto len = recvfrom(td.fd, buffer, sizeof buffer, 0,
			reinterpret_cast<sockaddr *>(&client), &clientlen);

	if (len < 0) {
		if (errno != EAGAIN) {
			throw_errno("recvfrom");
		}
		return len;
	}

	clientlen = sizeof(client);
	return do_echo(gd, td, buffer, len, &client, clientlen);
}

void echoer(global_data_t& gd, thread_data_t& td)
{
	pollfd fds = { td.fd, POLLIN, 0 };

	try {
		while (true) {
			int res = ::poll(&fds, 1, 1);
			if (res < 0) {
				if (errno == EAGAIN) continue;
				throw_errno("poll");
			}

			bool ready = true;
			while (ready) {
				res = echo_one(gd, td);
				if (res < 0) {
					ready = false;
				}
			}
		}
	} catch (std::logic_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}

void set_rx_ring(thread_data_t& td)
{
	tpacket_req& tp = td.tp;

	memset(&tp, 0, sizeof(tp));
	tp.tp_block_size = 4096;
	tp.tp_frame_size = 2048;
	tp.tp_block_nr = 4;
	tp.tp_frame_nr = 8;


	if (setsockopt(td.fd, SOL_PACKET, PACKET_RX_RING, &tp, sizeof(tp)) < 0) {
		throw_errno("setsockopt(PACKET_RX_RING)");
	}

	size_t size = tp.tp_block_size * tp.tp_block_nr;

	td.map = reinterpret_cast<uint8_t*>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, td.fd, 0));
	if (td.map == MAP_FAILED) {
		throw_errno("mmap");
	}
}

void echo_rx_ring(global_data_t& gd, thread_data_t& td)
{
	// enable PACKET_RX_RING / MMAP mode
	set_rx_ring(td);

	try {
		ptrdiff_t ll_offset = TPACKET_ALIGN(sizeof(struct tpacket_hdr));
		pollfd fds = { td.fd, POLLIN, 0 };

		while (true) {
			int res = ::poll(&fds, 1, -1);
			if (res < 0) {
				if (errno == EAGAIN) continue;
				throw_errno("poll");
			}

			for (unsigned int i = 0; i < td.tp.tp_frame_nr; ++i) {
				auto fp = td.map + i * td.tp.tp_frame_size;
				auto& hdr = *reinterpret_cast<tpacket_hdr*>(fp);

				if (hdr.tp_status == TP_STATUS_KERNEL) continue;

				auto& client = *reinterpret_cast<sockaddr_ll *>(fp + ll_offset);
				auto buf = (fp + hdr.tp_net);

				do_echo(gd, td, buf, hdr.tp_len, &client, 18);

				hdr.tp_status = TP_STATUS_KERNEL;
			}
		}
	} catch (std::logic_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}

int main(int argc, char *argv[])
{
	try {
		global_data_t		gd;

		gd.dest_port = 8053;
		gd.ifindex = if_nametoindex("enp5s0f1");
		if (gd.ifindex == 0) {
			throw_errno("if_nametoindex");
		}

		int n = 12;

		std::thread echo_thread[n];
		thread_data_t thread_data[n];

		for (int i = 0; i < n; ++i) {
			auto& td = thread_data[i];

			td.index = i;
			td.fd = socket_open(gd.ifindex);
			if (td.fd < 0) {
				throw std::runtime_error("couldn't open socket");
			}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)
			// kernel QDISC bypass
			if (socket_setopt(td.fd, PACKET_QDISC_BYPASS, 0) < 0) {
				throw_errno("setsockopt PACKET_QDISC_BYPASS");
			}
#endif

			echo_thread[i] = std::thread(echoer, std::ref(gd), std::ref(td));

			cpu_set_t cpu;
			CPU_ZERO(&cpu);
			CPU_SET(i, &cpu);
			pthread_setaffinity_np(echo_thread[i].native_handle(), sizeof(cpu), &cpu);
		}

		for (int i = 0; i < n; ++i) {
			echo_thread[i].join();
		}

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
