#include <iostream>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <thread>

#include <unistd.h>
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
	PacketSocket			packet;
	tpacket_req			tp;
	uint8_t*			map;
} thread_data_t;

typedef struct {
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

	auto res = sendto(td.packet.fd, buffer, len, MSG_DONTWAIT, reinterpret_cast<const sockaddr *>(client), clientlen);
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

	auto len = recvfrom(td.packet.fd, buffer, sizeof buffer, 0,
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

void echo_normal(global_data_t& gd, thread_data_t& td)
{
	try {
		while (true) {
			if (td.packet.poll(1) <= 0) continue;

			while (true) {
				if (echo_one(gd, td) <= 0) break;
			}
		}
	} catch (std::logic_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}

void set_rx_ring(thread_data_t& td)
{
	auto& tp = td.tp;

	memset(&tp, 0, sizeof(tp));
	tp.tp_block_size = 4096;
	tp.tp_frame_size = 2048;
	tp.tp_block_nr = 64;
	tp.tp_frame_nr = 128;

	td.map = reinterpret_cast<uint8_t*>(td.packet.rx_ring(tp));
}

void echo_rx_ring(global_data_t& gd, thread_data_t& td)
{
	// enable PACKET_RX_RING / MMAP mode
	set_rx_ring(td);

	try {
		ptrdiff_t ll_offset = TPACKET_ALIGN(sizeof(struct tpacket_hdr));
		int current = 0;

		while (true) {
			auto fp = td.map + current * td.tp.tp_frame_size;
			auto& hdr = *reinterpret_cast<tpacket_hdr*>(fp);

			if ((hdr.tp_status & TP_STATUS_USER) == 0) {
				if (td.packet.poll() <= 0) continue;
			}

			auto& client = *reinterpret_cast<sockaddr_ll *>(fp + ll_offset);
			auto buf = (fp + hdr.tp_net);

			do_echo(gd, td, buf, hdr.tp_len, &client, sizeof(sockaddr_ll));
			hdr.tp_status = TP_STATUS_KERNEL;
			current = (current + 1) % td.tp.tp_frame_nr;
		}
	} catch (std::logic_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}

int main(int argc, char *argv[])
{
	const std::string	ifname = "enp5s0f1";
	const uint16_t		port(8053);
	const unsigned int	threads = 12;

	try {
		global_data_t		gd;

		gd.dest_port = port;

		std::thread echo_thread[threads];
		thread_data_t thread_data[threads];

		for (auto i = 0U; i < threads; ++i) {

			auto& td = thread_data[i];

			td.packet.open();
			td.packet.bind(ifname);

			echo_thread[i] = std::thread(echo_rx_ring, std::ref(gd), std::ref(td));

			cpu_set_t cpu;
			CPU_ZERO(&cpu);
			CPU_SET(i, &cpu);
			pthread_setaffinity_np(echo_thread[i].native_handle(), sizeof(cpu), &cpu);
		}

		for (auto i = 0U; i < threads; ++i) {
			echo_thread[i].join();
		}

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
