/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

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

// per thread state
typedef struct {
	PacketSocket			packet;
	uint64_t			poll_count = 0;
	uint64_t			rx_count = 0;
} thread_data_t;

// top level state
typedef struct {
	uint16_t			dest_port;
} global_data_t;

global_data_t gd;

//
// receives a raw packet buffer, flips the source and destination
// addresses and ports, and sends if back out of the socket
//
ssize_t do_echo(uint8_t *buffer, size_t buflen,
		const sockaddr_ll *addr,
		void *userdata)
{
	auto& td = *reinterpret_cast<thread_data_t*>(userdata);
	auto& ip = *reinterpret_cast<iphdr *>(buffer);
	auto& udp = *reinterpret_cast<udphdr *>(buffer + 4 * ip.ihl);

	// ignore packets that aren't actually for us
	if (udp.dest != htons(gd.dest_port)) {
		return 0;
	}

	// reverse the packet source and address
	std::swap(ip.saddr, ip.daddr);
	std::swap(udp.source, udp.dest);

	// throw it back again
	auto res = sendto(td.packet.fd, buffer, buflen, MSG_DONTWAIT,
			reinterpret_cast<const sockaddr *>(addr), sizeof(*addr));
	if (res < 0 && errno != EAGAIN) {
		throw_errno("sendto");
	}

	return res;
}

//
// main thread worker function
//
void echo_rx_ring(thread_data_t& td)
{
	try {
		// enable PACKET_RX_RING mode 
		td.packet.rx_ring_enable(9, 4096);	// frame size = 512

		// continually take packets from the ring
		while (true) {
			td.packet.rx_ring_next(do_echo, -1, &td);
		}
	} catch (std::logic_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}

void __attribute__((__noreturn__)) usage(int result = EXIT_FAILURE)
{
	using namespace std;

	cout << "dnsecho [-p <port>] -i <ifname> [-T <threads>]" << endl;
	cout << "  -i the interface on which to listen" << endl;
	cout << "  -p the port on which to listen (default: 8053)" << endl;
	cout << "  -T the number of threads to run (default: ncpus)" << endl;

	exit(result);
}

int main(int argc, char *argv[])
{
	// default command line parameters
	const char *ifname = nullptr;
	uint16_t port = 8053;
	uint16_t threads = std::thread::hardware_concurrency();

	// standard getopt handling
	int opt;
	while ((opt = getopt(argc, argv, "i:p:T:h")) != -1) {
		switch (opt) {
			case 'i': ifname = optarg; break;
			case 'p': port = atoi(optarg); break;
			case 'T': threads = atoi(optarg); break;
			case 'h': usage(EXIT_SUCCESS);
			default: usage();
		}
	}

	// check that parameter requirements are met
	if ((optind < argc) || !ifname || threads < 1 || port == 0) {
		usage();
	}

	try {
		gd.dest_port = port;

		// create the specified number of threads
		std::thread echo_thread[threads];
		thread_data_t thread_data[threads];

		for (auto i = 0U; i < threads; ++i) {

			// create a socket per thread
			auto& td = thread_data[i];
			td.packet.open();
			td.packet.bind(ifname);

			echo_thread[i] = std::thread(echo_rx_ring, std::ref(td));

			// assign the thread to the same CPU core
			cpu_set_t cpu;
			CPU_ZERO(&cpu);
			CPU_SET(i, &cpu);
			pthread_setaffinity_np(echo_thread[i].native_handle(), sizeof(cpu), &cpu);
		}

		// wait for all of the threads to terminate (which
		// never actually happens)
		for (auto i = 0U; i < threads; ++i) {
			echo_thread[i].join();
		}

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
