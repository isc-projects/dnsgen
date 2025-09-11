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

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include "queryfile.h"

// EDNS flag constants
constexpr uint16_t EDNS_DO_BIT = 0x8000;  // DNSSEC OK bit

// via https://stackoverflow.com/a/2072890/6782
inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void usage(const char* progname) {
	std::cerr << "usage: " << progname << " [-e] [-D] <txtfile>" << std::endl;
	std::cerr << "  -e    Add EDNS OPT RR to queries" << std::endl;
	std::cerr << "  -D    Add EDNS OPT RR with DO (DNSSEC OK) bit" << std::endl;
}

int main(int argc, char *argv[])
{
	bool add_edns = false;
	bool add_dnssec = false;
	int opt;
	
	while ((opt = getopt(argc, argv, "eDh")) != -1) {
		switch (opt) {
		case 'e':
			add_edns = true;
			break;
		case 'D':
			add_dnssec = true;
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}
	
	if (optind >= argc) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	try {
		QueryFile	qf;

		// remove .txt extension if found
		std::string input(argv[optind]);
		std::string output = input;

		if (ends_with(output, ".txt")) {
			output.erase(output.length() - 4);
		}

		// append .raw
		output += ".raw";

		// start the conversion
		qf.read_txt(input);
		
		// add EDNS if requested (-D overrides -e)
		if (add_dnssec) {
			qf.edns(4096, EDNS_DO_BIT);  // 4KB buffer, DO bit set
		} else if (add_edns) {
			qf.edns(4096, 0);  // 4KB buffer, no flags
		}
		
		qf.write_raw(output);

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
