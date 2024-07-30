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
#include "queryfile.h"

// via https://stackoverflow.com/a/2072890/6782
inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		std::cerr << "usage: dnscvt <txtfile>" << std::endl;
		return EXIT_FAILURE;
	}

	try {
		QueryFile	qf;

		// remove .txt extension if found
		std::string input(argv[1]);
		std::string output = input;

		if (ends_with(output, ".txt")) {
			output.erase(output.length() - 4);
		}

		// append .raw
		output += ".raw";

		// start the conversion
		qf.read_txt(input);
		qf.write_raw(output);

	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
