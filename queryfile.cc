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

#include <cstdio>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cerrno>
#include <map>
#include <algorithm>

#include <arpa/inet.h>		// for ntohs() etc
#include <resolv.h>		// for res_mkquery()

#include "queryfile.h"
#include "util.h"

//
// from https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml
//
static std::map<std::string, uint16_t> type_map = {
	{ "A",		    1 },
	{ "NS",		    2 },
	{ "MD",		    3 },
	{ "MF",		    4 },
	{ "CNAME",	    5 },
	{ "SOA",	    6 },
	{ "MB",		    7 },
	{ "MG",		    8 },
	{ "MR",		    9 },
	{ "NULL",	   10 },
	{ "WKS",	   11 },
	{ "PTR",	   12 },
	{ "HINFO",	   13 },
	{ "MINFO",	   14 },
	{ "MX",		   15 },
	{ "TXT",	   16 },
	{ "RP",		   17 },
	{ "AFSDB",	   18 },
	{ "X25",	   19 },
	{ "ISDN",	   20 },
	{ "RT",		   21 },
	{ "NSAP",	   22 },
	{ "NSAP-PTR",	   23 },
	{ "SIG",	   24 },
	{ "KEY",	   25 },
	{ "PX",		   26 },
	{ "GPOS",	   27 },
	{ "AAAA",	   28 },
	{ "LOC",	   29 },
	{ "NXT",	   30 },
	{ "EID",	   31 },
	{ "NIMLOC",	   32 },
	{ "SRV",	   33 },
	{ "ATMA",	   34 },
	{ "NAPTR",	   35 },
	{ "KX",		   36 },
	{ "CERT",	   37 },
	{ "A6",		   38 },
	{ "DNAME",	   39 },
	{ "SINK",	   40 },
	{ "OPT",	   41 },
	{ "APL",	   42 },
	{ "DS",		   43 },
	{ "SSHFP",	   44 },
	{ "IPSECKEY",	   45 },
	{ "RRSIG",	   46 },
	{ "NSEC",	   47 },
	{ "DNSKEY",	   48 },
	{ "DHCID",	   49 },
	{ "NSEC3",	   50 },
	{ "NSEC3PARAM",	   51 },
	{ "TLSA",	   52 },
	{ "SMIMEA",	   53 },
	{ "HIP",	   55 },
	{ "NINFO",	   56 },
	{ "RKEY",	   57 },
	{ "TALINK",	   58 },
	{ "CDS",	   59 },
	{ "CDNSKEY",	   60 },
	{ "OPENPGPKEY",	   61 },
	{ "CSYNC",	   62 },
	{ "SPF",	   99 },
	{ "UINFO",	  100 },
	{ "UID",	  101 },
	{ "GID",	  102 },
	{ "UNSPEC",	  103 },
	{ "NID",	  104 },
	{ "L32",	  105 },
	{ "L64",	  106 },
	{ "LP",		  107 },
	{ "EUI48",	  108 },
	{ "EUI64",	  109 },
	{ "TKEY",	  249 },
	{ "TSIG",	  250 },
	{ "IXFR",	  251 },
	{ "AXFR",	  252 },
	{ "MAILB",	  253 },
	{ "MAILA",	  254 },
	{ "ANY",	  255 },
	{ "URI",	  256 },
	{ "CAA",	  257 },
	{ "AVC",	  258 },
	{ "DOA",	  259 },
	{ "TA",		32768 },
	{ "DLV",	32769 }
};

//
// Converts an RR type string to its numeric equivalent
//
// for performance, the match against the uppercase table
// above is always done first (since the input files are
// typically in upper case) and only if that lookup fails
// does the code then create a temporary upper-cased
// version of the input and recursively calls itself
//
static uint16_t type_to_number(const std::string& type, bool case_insensitive = true)
{
	auto itr = type_map.find(type);
	if (itr != type_map.end()) {
		return itr->second;
	} else if (type.compare(0, 4, "TYPE", 4) == 0) {
		size_t index;
		std::string num = type.substr(4, std::string::npos);
		try {
			unsigned long val = std::stoul(num, &index, 10);
			if (num.cbegin() + index != num.cend()) {
				throw std::runtime_error("numeric QTYPE trailing garbage");
			} else if (val > std::numeric_limits<uint16_t>::max()) {
				throw std::runtime_error("numeric QTYPE out of range");
			} else {
				return type_map[type] = val;
			}
		} catch (std::logic_error& e) {
			throw std::runtime_error("numeric QTYPE unparseable");
		}
	} else {
		// search again using the upper-cased version of the string
		if (case_insensitive) {
			std::string tmp(type);
			std::transform(tmp.cbegin(), tmp.cend(), tmp.begin(), ::toupper);
			return type_map[type] = type_to_number(tmp, false);
		} else {
			throw std::runtime_error("unrecognised QTYPE: " + type);
		}
	}
}

//
// creates a Record entry from the given qname and qtype
//
static QueryFile::Record make_record(const std::string& name, const std::string& type)
{
	QueryFile::Record record;
	record.resize(12 + 255 + 4);	// maximum question section

	uint16_t qtype = type_to_number(type);

	int n = res_mkquery(0, name.c_str(), 1, qtype, nullptr, 0, nullptr,
			    record.data(), record.size());
	if (n < 0) {
		throw std::runtime_error("couldn't parse domain name");
	} else {
		record.resize(n);
		return record;
	}
}

//
// Loads a text file (in dnsperf format)
//
void QueryFile::read_txt(const std::string& filename)
{
	std::ifstream file(filename);
	if (!file) {
		throw_errno("opening query file");
	}

	storage_t list;
	std::string name, type;
	size_t line_no = 0;

	while (file >> name >> type) {
		line_no++;

		try {
			Record record;
			list.push_back(make_record(name, type));
		} catch (std::runtime_error &e) {
			std::string error = "reading query file at line "
					+ std::to_string(line_no)
					+ ": " + e.what();
			throw_errno(error);
		}
	}

	file.close();

	std::swap(queries, list);
}

//
// Loads a raw input file (<16 bit network order length><payload...>)
//
void QueryFile::read_raw(const std::string& filename)
{
	std::ifstream file(filename, std::ifstream::binary);
	if (!file) {
		throw_errno("opening query file");
	}

	storage_t list;
	uint16_t len;

	while (file) {
		if (file.read(reinterpret_cast<char*>(&len), sizeof(len))) {

			len = ntohs(len);		// swap to host order

			Record record;
			record.resize(len);

			if (file.read(reinterpret_cast<char*>(record.data()), len)) {
				list.push_back(record);
			}
		}
	}

	file.close();

	std::swap(queries, list);
}

//
// Saves the query set in raw format
//
void QueryFile::write_raw(const std::string& filename) const
{
	std::ofstream file(filename, std::ifstream::binary);
	if (!file) {
		throw_errno("opening query file");
	}

	for (const auto& query: queries) {
		uint16_t len = htons(query.size());	// big-endian
		file.write(reinterpret_cast<const char*>(&len), sizeof(len));
		file.write(reinterpret_cast<const char*>(query.data()), query.size());
	}

	file.close();
}

//
// Adds an EDNS OPT RR to every record in the QueryFile with
// the specified UDP buffer length and flags
//
void QueryFile::edns(const uint16_t buflen, uint16_t flags)
{
	std::vector<uint8_t> opt = {
		0,					// name
		0, 41,					// type = OPT
		static_cast<uint8_t>(buflen >> 8),	// buflen MSB
		static_cast<uint8_t>(buflen >> 0),	// buflen LSB
		0,					// xrcode = 0,
		0,					// version = 0,
		static_cast<uint8_t>(flags >> 8),	// flags MSB
		static_cast<uint8_t>(flags >> 0),	// flags LSB
		0, 0					// rdlen = 0
	};

	for (auto& query: queries) {

		// adjust ARCOUNT
		auto* p = reinterpret_cast<uint16_t*>(query.data());
		p[5] = htons(ntohs(p[5]) + 1);

		query.reserve(query.size() + 11);
		query.insert(query.end(), opt.cbegin(), opt.cend());
	}
}
