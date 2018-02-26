#include <iostream>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <linux/if_ether.h>

#include "datafile.h"
#include "packet.h"
#include "util.h"

typedef struct {
	PacketSocket			packet;
	uint16_t			index;
	uint16_t			port_base;
	uint16_t			port_count;
	uint16_t			port_offset;
	uint16_t			ip_id;
	uint16_t			query_id;
	uint64_t			tx_count;
	uint64_t			rx_count;
	size_t				query_num;
} thread_data_t;

typedef struct {
	int				thread_count;
	uint16_t			ifindex;
	uint16_t			dest_port;
	in_addr_t			src_ip;
	in_addr_t			dest_ip;
	Datafile			query;
	size_t				query_count;
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
	auto query = gd.query[td.query_num];
	td.query_num += gd.thread_count;
	if (td.query_num > gd.query_count) {
		td.query_num -= gd.query_count;
	}

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

	auto res = sendmsg(td.packet.fd, &msg, 0);
	if (res < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
		throw_errno("sendmsg");
	}
	return res;
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
			throw_errno("sendmsg");
		} else {
			++td.tx_count;
			timespec ts;
			for (int i = 0; i < 0; ++i) {
				(void) clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
			}
		}
	}
}

ssize_t receive_one(uint8_t *buffer, size_t buflen, const sockaddr_ll *addr, void *userdata)
{
	auto &td = *reinterpret_cast<thread_data_t*>(userdata);
	++td.rx_count;
	return 0;
}

void receiver(global_data_t& gd, thread_data_t& td)
{
	td.packet.rx_ring_enable(11, 128);	// frame size = 2048
	while (!gd.stop) {
		td.packet.rx_ring_next(receive_one, 10, &td);
	}
}

int main(int argc, char *argv[])
{
	const std::string ifname = "enp5s0f1";

	try {
		global_data_t		gd;

		gd.ifindex = if_nametoindex("enp5s0f1");
		gd.query.read_raw("/tmp/queryfile-example-current.raw");
		gd.query_count = gd.query.size();
		gd.dest_port = 8053;
		gd.src_ip = inet_addr("10.255.255.245");
		gd.dest_ip = inet_addr("10.255.255.244");
		gd.start = false;
		gd.stop = false;
		gd.thread_count = 12;

		int n = gd.thread_count;

		std::thread sender_thread[n], receiver_thread[n];
		thread_data_t thread_data[n];

		for (int i = 0; i < n; ++i) {
			auto& td = thread_data[i];

			memset(&td, 0, sizeof td);
			td.index = i;
			td.packet.open();
			td.packet.bind(gd.ifindex);

			td.port_count = 4096;
			td.port_base = 16384 + td.port_count * i;
			td.tx_count = 0;
			td.rx_count = 0;

			sender_thread[i] = std::thread(sender, std::ref(gd), std::ref(td));
			receiver_thread[i] = std::thread(receiver, std::ref(gd), std::ref(td));

			cpu_set_t cpu;
			CPU_ZERO(&cpu);
			CPU_SET(i, &cpu);
			pthread_setaffinity_np(sender_thread[i].native_handle(), sizeof(cpu), &cpu);

			CPU_ZERO(&cpu);
			CPU_SET(i, &cpu);
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
