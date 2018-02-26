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
	uint64_t			poll_count = 0;
	uint64_t			rx_count = 0;
} thread_data_t;

typedef struct {
	uint16_t			dest_port;
} global_data_t;

global_data_t gd;

ssize_t do_echo(uint8_t *buffer, size_t buflen,
		const sockaddr_ll *addr,
		void *userdata)
{
	auto& td = *reinterpret_cast<thread_data_t*>(userdata);

	auto& ip = *reinterpret_cast<iphdr *>(buffer);
	auto& udp = *reinterpret_cast<udphdr *>(buffer + 4 * ip.ihl);

	if (udp.dest != htons(gd.dest_port)) {
		return 0;
	}

	std::swap(ip.saddr, ip.daddr);
	std::swap(udp.source, udp.dest);

	auto res = sendto(td.packet.fd, buffer, buflen, MSG_DONTWAIT,
			reinterpret_cast<const sockaddr *>(addr), sizeof(*addr));
	if (res < 0 && errno != EAGAIN) {
		throw_errno("sendto");
	}

	return res;
}

ssize_t echo_one(thread_data_t& td)
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

	return do_echo(buffer, len, &client, &td);
}

void echo_normal(thread_data_t& td)
{
	try {
		while (true) {
			if (td.packet.poll(1) <= 0) continue;

			while (true) {
				if (echo_one(td) <= 0) break;
			}
		}
	} catch (std::logic_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}

void echo_rx_ring(thread_data_t& td)
{
	try {
		td.packet.rx_ring_enable(11, 128);	// frame size = 2048 
		while (true) {
			td.packet.rx_ring_next(do_echo, -1, &td);
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
		gd.dest_port = port;

		std::thread echo_thread[threads];
		thread_data_t thread_data[threads];

		for (auto i = 0U; i < threads; ++i) {

			auto& td = thread_data[i];

			td.packet.open();
			td.packet.bind(ifname);

			echo_thread[i] = std::thread(echo_rx_ring, std::ref(td));

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
