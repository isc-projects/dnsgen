#include <iostream>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "datafile.h"
#include "packet.h"

typedef struct {
	int				fd;
	uint16_t			index;
	uint16_t			port_base;
	uint16_t			port_count;
	uint16_t			port_offset;
	uint16_t			ip_id;
	uint16_t			query_id;
	uint64_t			tx_count;
	uint64_t			rx_count;
} thread_data_t;

typedef struct {
	uint16_t			ifindex;
	uint16_t			dest_port;
	in_addr_t			src_ip;
	in_addr_t			dest_ip;
	Datafile			queries;
	std::atomic<bool>		stop;
	bool				start;
	std::mutex			mutex;
	std::condition_variable		cv;
} global_data_t;

typedef struct __attribute__((packed)) {
	struct iphdr			ip;
	struct udphdr			udp;
} header_t;

static uint16_t checksum(const iphdr& hdr)
{
	uint32_t sum = 0;

	auto p = reinterpret_cast<const uint16_t *>(&hdr);
	for (int i = 0, n = hdr.ihl * 2; i < n; ++i) {	//	.ihl = length / 4
		sum += ntohs(*p++);
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return static_cast<uint16_t>(~sum);
}

ssize_t send_one(global_data_t& gd, thread_data_t& td, sockaddr_ll& addr)
{
	auto query = gd.queries.next();

	uint16_t payload_size = query.size();
	uint16_t udp_size = payload_size + sizeof(udphdr);
	uint16_t tot_size = udp_size + sizeof(iphdr);

	header_t h = { { 0, }};

	h.ip.ihl = 5;		// sizeof(iphdr) / 4
	h.ip.version = 4;
	h.ip.ttl = 8;
	h.ip.protocol = IPPROTO_UDP;
	h.ip.id = htons(td.ip_id++);
	h.ip.saddr = gd.src_ip;
	h.ip.daddr = gd.dest_ip;
	h.ip.tot_len = htons(tot_size);
	h.ip.check = htons(checksum(h.ip));

	td.port_offset = (td.port_offset + 1) % td.port_count;

	h.udp.source = htons(td.port_base + td.port_offset);
	h.udp.dest = htons(gd.dest_port);
	h.udp.len = htons(udp_size);

	iovec iov[2] = {
		{
			reinterpret_cast<char *>(&h),
			sizeof(h)
		},
		{
			const_cast<char *>(reinterpret_cast<const char *>(query.data())),
			query.size()
		}
	};

	msghdr msg = {
		reinterpret_cast<void *>(&addr), sizeof(addr), iov, 2,
		nullptr, 0, 0
	};

	return sendmsg(td.fd, &msg, 0);
}

void sender(global_data_t& gd, thread_data_t& td)
{
	std::array<uint8_t, 6> mac = { { 0x3c, 0xfd, 0xfe, 0x03, 0xb6, 0x62 } };

	static sockaddr_ll addr = { 0 };
	addr.sll_family = AF_PACKET;
	addr.sll_ifindex = gd.ifindex;
	addr.sll_protocol = htons(ETH_P_IP);
	addr.sll_halen = IFHWADDRLEN;
	memcpy(addr.sll_addr, mac.data(), 6);

	// wait for start condition
	{
		std::unique_lock<std::mutex> lock(gd.mutex);
		while (!gd.start) gd.cv.wait(lock);
	}

	while (!gd.stop) {
		if (send_one(gd, td, addr) < 0) {
			if (errno == EAGAIN) continue;
			throw std::system_error(errno, std::system_category(), "sendmsg");
		} else {
			++td.tx_count;
			timespec ts;
			for (int i = 0; i < 6; ++i) {
				(void) clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
			}
		}
	}
}

ssize_t receive_one(global_data_t& gd, thread_data_t& td)
{
	uint8_t buffer[4096];
	sockaddr_storage client;
	socklen_t clientlen = sizeof(client);

	auto len = recvfrom(td.fd, buffer, sizeof buffer, MSG_DONTWAIT,
			reinterpret_cast<sockaddr *>(&client), &clientlen);

	return len;
}

void receiver(global_data_t& gd, thread_data_t& td)
{
	pollfd fds = { td.fd, POLLIN, 0 };

	while (!gd.stop) {
		int res = ::poll(&fds, 1, 10);
		if (res < 0) {			// error
			if (errno == EAGAIN) continue;
			throw std::system_error(errno, std::system_category(), "poll");
		} else if (res == 0) {		// timeout
			continue;
		}

		bool ready = true;
		while (ready) {
			res = receive_one(gd, td);
			if (res < 0) {
				ready = false;
				if (errno == EAGAIN) break;
				throw std::system_error(errno, std::system_category(), "recvfrom");
			} else {
				++td.rx_count;
			}
		}
	}
}

int main(int argc, char *argv[])
{
	try {
		global_data_t		gd;

		gd.queries.read_raw("/tmp/queryfile-example-current.raw");
		gd.dest_port = 8053;
		gd.src_ip = inet_addr("10.255.255.245");
		gd.dest_ip = inet_addr("10.255.255.244");
		gd.start = false;
		gd.stop = false;
		gd.ifindex = if_nametoindex("enp5s0f1");
		if (gd.ifindex == 0) {
			throw std::system_error(errno, std::system_category(), "if_nametoindex");
		}

		int n = 12;

		std::thread sender_thread[n], receiver_thread[n];
		thread_data_t thread_data[n];

		for (int i = 0; i < n; ++i) {
			auto& td = thread_data[i];

			td.index = i;
			td.fd = get_socket(gd.ifindex);
			td.port_base = 16384 + 4096 * i;
			td.port_count = 4096;
			td.tx_count = 0;
			td.rx_count = 0;

			sender_thread[i] = std::thread(sender, std::ref(gd), std::ref(td));
			receiver_thread[i] = std::thread(receiver, std::ref(gd), std::ref(td));

			cpu_set_t cpu;
			CPU_ZERO(&cpu);
			CPU_SET(i, &cpu);
			pthread_setaffinity_np(sender_thread[i].native_handle(), sizeof(cpu), &cpu);
			pthread_setaffinity_np(receiver_thread[i].native_handle(), sizeof(cpu), &cpu);
		}

		auto timer = std::thread([&gd]() {
			{
				std::unique_lock<std::mutex> lock(gd.mutex);
				gd.start = true;
			}
			gd.cv.notify_all();
			sleep(30);
			gd.stop = true;
		});

		for (int i = 0; i < n; ++i) {
			sender_thread[i].join();
			receiver_thread[i].join();
		}

		timer.join();

		for (int i = 0; i < n; ++i) {
			auto& td = thread_data[i];
			std::cerr << "tx[" << i << "] = " << td.tx_count << std::endl;
		}

		uint64_t rx_total = 0;
		for (int i = 0; i < n; ++i) {
			auto& td = thread_data[i];
			rx_total += td.rx_count;
			std::cerr << "rx[" << i << "] = " << td.rx_count << std::endl;
		}
		std::cerr << "rx[total] = " << rx_total << std::endl;

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
