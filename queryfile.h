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

#include <string>
#include <vector>
#include <deque>

class QueryFile {

public:
	typedef std::vector<uint8_t>	Record;

private:
	typedef std::deque<Record>	storage_t;
	storage_t			queries;

public:
	void				read_txt(const std::string& filename);
	void				read_raw(const std::string& filename);
	void				write_raw(const std::string& filename) const;
	void				edns(const uint16_t buflen, uint16_t flags);

public:

	const Record&			operator[](size_t n) const {
		return queries[n];
	};

	size_t				size() const {
		return queries.size();
	};
};
