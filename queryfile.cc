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

static std::map<std::string, uint16_t> type_map = {
	{ "A",		  1 },
	{ "SOA",	  6 },
	{ "PTR",	 12 },
	{ "MX",		 15 },
	{ "TXT",	 16 },
	{ "AAAA",	 28 },
	{ "SRV",	 33 },
};

static uint16_t type_to_number(const std::string& type, bool check_case = true)
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
		if (check_case) {
			std::string tmp(type);
			std::transform(tmp.cbegin(), tmp.cend(), tmp.begin(), ::toupper);
			return type_map[type] = type_to_number(tmp, false);
		} else {
			throw std::runtime_error("unrecognised QTYPE: " + type);
		}
	}
}

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
