#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <queue>
#include <numeric>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ether.h>
#include <linux/if_ether.h>

#include "queryfile.h"
#include "packet.h"
#include "timer.h"
#include "util.h"

static std::exception_ptr globex = nullptr;

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
	size_t				batch_size;
	uint16_t			ifindex;
	uint16_t			dest_port;
	in_addr_t			src_ip;
	in_addr_t			dest_ip;
	ether_addr			dest_mac;
	QueryFile			query;
	size_t				query_count;
	std::atomic<uint32_t>		rx_count;
	std::atomic<uint32_t>		tx_count;
	std::atomic<uint32_t>		rate;
	std::atomic<bool>		stop;
	bool				start;
	bool				rampmode;
	unsigned int			runtime;
	unsigned int			increment;
	unsigned int			stats_out;
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

std::string thread_getname(std::thread& t)
{
	char buf[17];
	pthread_getname_np(t.native_handle(), buf, sizeof buf);
	return std::string(buf);
}

std::string thread_getname()
{
	char buf[17];
	pthread_getname_np(pthread_self(), buf, sizeof buf);
	return std::string(buf);
}

void thread_setname(std::thread& t, const std::string& name)
{
	pthread_setname_np(t.native_handle(), name.c_str());
}

void thread_setcpu(std::thread& t, unsigned int n)
{
	cpu_set_t cpu;
	CPU_ZERO(&cpu);
	CPU_SET(n, &cpu);
	pthread_setaffinity_np(t.native_handle(), sizeof(cpu), &cpu);
}

ssize_t send_many(global_data_t& gd, thread_data_t& td, sockaddr_ll& addr)
{
	const auto n = gd.batch_size;
	mmsghdr msgs[n];
	header_t header[n];
	iovec iovecs[n * 2];

	for (size_t i = 0; i < n; ++i) {

		// get next n'th query from the data file
		auto& query = gd.query[td.query_num];
		td.query_num += gd.thread_count;
		if (td.query_num > gd.query_count) {
			td.query_num -= gd.query_count;
		}

		auto& hdr = msgs[i].msg_hdr;
		auto& pkt = header[i];

		int vn = i * 2;		// two iovecs per message
		iovecs[vn] =  {
			reinterpret_cast<char *>(&pkt),
			sizeof(pkt)
		};
		iovecs[vn + 1] = {
			const_cast<char *>(reinterpret_cast<const char *>(query.data())),
			query.size()
		};

		// fill out msghdr
		memset(&hdr, 0, sizeof(hdr));
		hdr.msg_iov = &iovecs[vn];
		hdr.msg_iovlen = 2;
		hdr.msg_name = reinterpret_cast<void *>(&addr);
		hdr.msg_namelen = sizeof(addr);

		// calculate header and message lengths
		uint16_t payload_size = query.size();
		uint16_t udp_size = payload_size + sizeof(udphdr);
		uint16_t tot_size = udp_size + sizeof(iphdr);

		// fill out IP header
		memset(&pkt, 0, sizeof(pkt));
		pkt.ip.ihl = 5;		// sizeof(iphdr) / 4
		pkt.ip.version = 4;
		pkt.ip.ttl = 8;
		pkt.ip.protocol = IPPROTO_UDP;
		pkt.ip.id = htons(td.ip_id++);
		pkt.ip.saddr = gd.src_ip;
		pkt.ip.daddr = gd.dest_ip;
		pkt.ip.tot_len = htons(tot_size);
		pkt.ip.check = htons(checksum(pkt.ip));

		// fill out UDP header
		pkt.udp.source = htons(td.port_base + td.port_offset);
		pkt.udp.dest = htons(gd.dest_port);
		pkt.udp.len = htons(udp_size);

		td.port_offset = (td.port_offset + 1) % td.port_count;
	}

	size_t offset = 0;

	while (offset < n)  {
		auto res = sendmmsg(td.packet.fd, &msgs[offset], n - offset, 0);
		if (res < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
			throw_errno("sendmmsg");
		}
		offset += res;
	}

	return offset;
}

void wait_for_start(global_data_t& gd)
{
	std::unique_lock<std::mutex> lock(gd.mutex);
	while (!gd.start) {
		gd.cv.wait(lock);
	}
}

void sender_loop(global_data_t& gd, thread_data_t& td)
{
	static sockaddr_ll addr = { 0 };
	addr.sll_family = AF_PACKET;
	addr.sll_ifindex = gd.ifindex;
	addr.sll_protocol = htons(ETH_P_IP);
	addr.sll_halen = IFHWADDRLEN;
	memcpy(addr.sll_addr, &gd.dest_mac, 6);

	// wait for start condition
	wait_for_start(gd);

	timespec now;
	timespec error = { 0, 0 };
	clock_gettime(CLOCK_MONOTONIC, &now);

	while (!gd.stop) {

		auto res = send_many(gd, td, addr);
		if (res	< 0) {
			if (errno == EAGAIN) continue;
			throw_errno("sendmsg");
		} else {
			gd.tx_count += res;
			td.tx_count += res;

			// artificial delay
			uint64_t delta = 1e9 * gd.batch_size * gd.thread_count / gd.rate;

			timespec next = now + delta - error;
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
			clock_gettime(CLOCK_MONOTONIC, &now);
			error = now - next;
		}
	}
}

void sender(global_data_t& gd, thread_data_t& td)
{
	try {
		sender_loop(gd, td);
	} catch (...) {
		globex = std::current_exception();
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
	try {
		td.packet.rx_ring_enable(11, 1024);	// frame size = 1 << 11 = 2048
		while (!gd.stop) {
			if (td.packet.rx_ring_next(receive_one, 10, &td)) {
				++gd.rx_count;
			}
		}
	} catch (...) {
		globex = std::current_exception();
	}
}

void rate_adapter(global_data_t& gd)
{
	const uint64_t interval = 1e8;
	const int qsize = 20;
	uint32_t rx_max = 0;
	std::deque<uint32_t> rates;

	wait_for_start(gd);

	timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);

	do {
		// wait for the next clock interval
		next = next + interval;
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

		// accumulate and average the last 'n' readings
		rates.push_back(gd.rx_count);
		if (rates.size() > qsize) {
			rates.pop_front();
		}
		auto rx_average = std::accumulate(rates.cbegin(), rates.cend(), 0U) / rates.size();

		// convert into per second rate and record max achieved
		uint32_t rx_rate = 1e9 * rx_average / interval;
		rx_max = std::max(rx_rate, rx_max);

		// show stats
		using namespace std;
		cout << next << " ";
		cout << gd.rate << " " << gd.tx_count << " " << gd.rx_count << " ";
		cout << endl;

		// adjust the rate for the next pass
		if (gd.rampmode) {
			gd.rate += gd.increment;
		} else {
			gd.rate = 0.5 * (rx_rate + rx_max) + gd.increment;
		}

		// reset the counters for the next pass
		gd.rx_count = 0;
		gd.tx_count = 0;

	} while (!gd.stop);

	std::cout << "Peak RX rate = " << rx_max << std::endl;
}

void life_timer(global_data_t& gd)
{
	{
		std::lock_guard<std::mutex> lock(gd.mutex);
		gd.start = true;
	}

	timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	start.tv_sec += 1;
	start.tv_nsec = 0;
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, nullptr);

	gd.cv.notify_all();

	timespec wakeup = { gd.runtime, 0 };
	clock_nanosleep(CLOCK_MONOTONIC, 0, &wakeup, nullptr);
	gd.stop = true;
}

void usage(int result = EXIT_FAILURE)
{
	using namespace std;

	cout << "dnsgen -i <ifname> -a <local_addr>" << endl;
        cout << "       -s <server_addr> [-p <port>] -m <server_mac_addr>" << endl;
        cout << "      [-T <threads>] [-l <timelimit>] -d <datafile>" << endl;
        cout << "      [-b <batchsize>] [-r <rate_start>] [-R <rate_increment>" << endl;
	cout << "  -s the server to query" << endl;
	cout << "  -p the port on which to query the server (default: 8053)" << endl;
	cout << "  -m the MAC address of the server to query" << endl;
	cout << "  -a the local address from which to send queries" << endl;
	cout << "  -d the input data file" << endl;
	cout << "  -T the number of threads to run (default: ncpus)" << endl;
	cout << "  -l run for at most this many seconds" << endl;
	cout << "  -b packet batch size" << endl;
	cout << "  -r initial packet rate (10000)" << endl;
	cout << "  -R packet rate increment (10000)" << endl;

	exit(result);
}

int main(int argc, char *argv[])
{
	global_data_t		gd;

	gd.thread_count = std::thread::hardware_concurrency();
	gd.batch_size = 32;
	gd.dest_port = 8053;
	gd.rate = 10000;
	gd.increment = 10000;
	gd.runtime = 30;
	gd.rampmode = false;

	const char *datafile = nullptr;
	const char *rawfile = nullptr;
	const char *ifname = nullptr;
	const char *src = nullptr;
	const char *dest = nullptr;
	const char *dest_mac = nullptr;

	int opt;
	while ((opt = getopt(argc, argv, "i:a:s:m:d:D:p:l:T:b:r:R:S:M")) != -1) {
		switch (opt) {
			case 'i': ifname = optarg; break;
			case 'a': src = optarg; break;
			case 's': dest = optarg; break;
			case 'm': dest_mac = optarg; break;
			case 'd': datafile = optarg; break;
			case 'D': rawfile = optarg; break;
			case 'p': gd.dest_port = atoi(optarg); break;
			case 'l': gd.runtime = atoi(optarg); break;
			case 'T': gd.thread_count= atoi(optarg); break;
			case 'b': gd.batch_size = atoi(optarg); break;
			case 'r': gd.rate = atoi(optarg); break;
			case 'R': gd.increment = atoi(optarg); break;
			case 'S': gd.stats_out = atoi(optarg); break;
			case 'M': gd.rampmode = true; break;
			case 'h': usage(EXIT_SUCCESS);
			default: usage();
		}
	}

	// check for extra args, or missing mandatory args
	if ((optind < argc) || !src || !dest || !dest_mac || !ifname) {
		usage();
	}

	// check for illegal args
	if ((gd.thread_count < 1) || (gd.runtime < 1) ||
	    (gd.batch_size < 1) || (gd.increment < 1))
	{
		usage();
	}

	// either rawfile or datafile must be specified (but not both)
	if ((!rawfile ^ !datafile) == false) {
		usage();
	}

	try {
		gd.ifindex = if_nametoindex(ifname);
		if (rawfile) {
			gd.query.read_raw(rawfile);
		} else {
			gd.query.read_txt(datafile);
		}
		gd.query_count = gd.query.size();
		gd.src_ip = inet_addr(src);
		gd.dest_ip = inet_addr(dest);
		gd.start = false;
		gd.stop = false;
		gd.rx_count = 0;
		gd.tx_count = 0;

		if (!ether_aton_r(dest_mac, &gd.dest_mac)) {
			throw std::runtime_error("invalid destination MAC");
		}

		auto rate = std::thread(rate_adapter, std::ref(gd));
		thread_setname(rate, "rate");

		int n = gd.thread_count;
		std::thread tx_thread[n], rx_thread[n];
		thread_data_t thread_data[n];

		for (int i = 0; i < n; ++i) {
			auto& td = thread_data[i];

			// memset(&td, 0, sizeof td);
			td.index = i;
			td.packet.open();
			td.packet.bind(gd.ifindex);

			td.query_num = i;
			td.port_count = 4096;
			td.port_base = 16384 + td.port_count * i;
			td.tx_count = 0;
			td.rx_count = 0;

			auto& tx = tx_thread[i] = std::thread(sender, std::ref(gd), std::ref(td));
			thread_setname(tx, std::string("tx:") + std::to_string(i));
			thread_setcpu(tx, i);

			auto& rx = rx_thread[i] = std::thread(receiver, std::ref(gd), std::ref(td));
			thread_setname(rx, std::string("rx:") + std::to_string(i));
			thread_setcpu(rx, i);
		}

		auto timer = std::thread(life_timer, std::ref(gd));
		thread_setname(timer, "timer");

		for (int i = 0; i < n; ++i) {
			tx_thread[i].join();
			rx_thread[i].join();
		}

		timer.join();
		rate.join();

		if (globex) {
			std::rethrow_exception(globex);
		}

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
