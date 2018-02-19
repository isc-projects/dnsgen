#include <cerrno>
#include <cstring>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

#if 1

#include "datafile.h"

#else

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "datafile.h"

typedef struct {
	int					fd;
	uint16_t			port_min;
	uint16_t			port_count;
	uint16_t			ip_id = 0;
	uint16_t			query_id = 0;
} thread_data_t;

typedef struct {
	in_addr_t			src_ip;
	in_addr_t			dest_ip;
	int					ifindex;
	uint16_t			dest_port;
	Datafile			data;
} global_data_t;

typedef struct __attribute__((packed)) {
	struct iphdr		ip;
	struct udphdr		udp;
	uint8_t				payload[512];
} query_t;

int get_socket(int ifindex)
{
	int fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
	if (fd < 0) {
		throw std::system_error(errno, std::system_category(), "socket AF_PACKET");
	}

	// bind the AF_PACKET socket to the specified interface
	sockaddr_ll saddr;
	memset(&saddr, 0, sizeof(saddr));
	saddr.sll_family = AF_PACKET;
	saddr.sll_ifindex = ifindex;
	if (saddr.sll_ifindex == 0) {
		throw std::system_error(errno, std::system_category(), "if_nametoindex");
	}

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

void sender(const global_data_t& gd, thread_data_t& td)
{
	query_t	query;

	query.ip.ihl = sizeof(query.ip) >> 2;
	query.ip.version = 4;
	query.ip.id = td.ip_id++;
	query.ip.frag_off = htons(0x4000);		// DNF
	query.ip.ttl = 2;
	query.ip.protocol = IPPROTO_UDP;
	query.ip.saddr = gd.src_ip;
	query.ip.daddr = gd.dest_ip;

	query.udp.source = htons(td.port_min);
	query.udp.dest = htons(gd.dest_port);
	query.udp.check = 0;

	query.udp.len = htons(sizeof(query.payload) + sizeof(query.udp));
	query.ip.tot_len = htons(sizeof(query.payload) + sizeof(query.udp) + sizeof(query.ip));

	sockaddr_ll daddr = { 0 };
	daddr.sll_family = AF_PACKET;
	daddr.sll_ifindex = gd.ifindex;
	daddr.sll_protocol = htons(ETH_P_IP);

	int err = sendto(td.fd,
		reinterpret_cast<const void *>(&query),
		540, 0, /* TMP length hack */
		reinterpret_cast<const sockaddr *>(&daddr), sizeof(daddr));

	std::cout << "sendto: " << err << " " << errno << std::endl;

	sleep(5);
	std::cout << "sender ending" << std::endl;
}

void receiver(const global_data_t& gd, thread_data_t& td)
{
	sleep(7);
	std::cout << "receiver ending" << std::endl;
}

#endif

int main(int argc, char *argv[])
{
	try {
		Datafile tmp;

		tmp.read_raw("/tmp/queryfile-example-current.raw");

		//tmp.read_txt("/tmp/queryfile-example-current");
		//tmp.write_raw("/tmp/queryfile-example-current.raw");

#if 0
		global_data_t		gd;
		thread_data_t		td1;

		gd.dest_port = 8053;
		gd.src_ip = inet_addr("10.1.2.5");
		gd.dest_ip = inet_addr("10.1.2.1");
		gd.ifindex = if_nametoindex("enp2s0");

		td1.fd = get_socket(gd.ifindex);
		std::thread sender_1(sender, std::ref(gd), std::ref(td1));
		std::thread receiver_1(receiver, std::ref(gd), std::ref(td1));

		sender_1.join();
		receiver_1.join();
#endif

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
