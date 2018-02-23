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
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <linux/version.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "packet.h"
#include "util.h"

typedef struct {
	int				fd;
	uint16_t			index;
} thread_data_t;

typedef struct {
	uint16_t			ifindex;
	uint16_t			dest_port;
	in_addr_t			src_ip;
	in_addr_t			dest_ip;
} global_data_t;

ssize_t echo_one(global_data_t& gd, thread_data_t& td)
{
	uint8_t buffer[4096];
	sockaddr_storage client;
	socklen_t clientlen = sizeof(client);

	auto len = recvfrom(td.fd, buffer, sizeof buffer, MSG_DONTWAIT,
			reinterpret_cast<sockaddr *>(&client), &clientlen);

	if (len < 0) {
		if (errno != EAGAIN) {
			throw_errno("recvfrom");
		}
		return len;
	}

	auto& ip = *reinterpret_cast<iphdr *>(buffer);
	auto& udp = *reinterpret_cast<udphdr *>(buffer + 4 * ip.ihl);

	if (udp.dest != htons(gd.dest_port)) {
		return 0;
	}

	std::swap(ip.saddr, ip.daddr);
	std::swap(udp.source, udp.dest);

	clientlen = sizeof(sockaddr_ll);
	len = sendto(td.fd, buffer, len, 0,
			reinterpret_cast<sockaddr *>(&client), clientlen);
	if (len < 0 && errno != EAGAIN) {
		throw_errno("sendto");
	}

	return len;
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

		// std::thread sender_thread[n], receiver_thread[n];
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
			// bypass the kernel QDISC
			if (socket_setopt(td.fd, PACKET_QDISC_BYPASS, 1) < 0) {
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
