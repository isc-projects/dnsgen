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

#pragma once

#include <cstddef>
#include <string>
#include <poll.h>
#include <linux/if_packet.h>

class PacketSocket {

public:
	typedef ssize_t	(*rx_callback_t)(uint8_t* buf, size_t buflen, const sockaddr_ll* addr, void *userdata);

private:
	pollfd		pfd;
	tpacket_req	req;

	uint8_t*	map = nullptr;
	uint32_t	rx_current = 0;
	ptrdiff_t	ll_offset;

public:
	int		fd = -1;

public:
			~PacketSocket();

public:
	void		open();
	void		close();

	void		bind(unsigned int ifindex);
	void		bind(const std::string& ifname);
	int		poll(int timeout = -1);

	int		setopt(int optname, const uint32_t val);
	int		getopt(int optname, uint32_t& val);

	void		rx_ring_enable(size_t frame_bits, size_t frame_nr);
	int		rx_ring_next(rx_callback_t cb, int timeout = -1, void *userdata = nullptr);
};
