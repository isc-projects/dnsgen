#include <map>
#include <stdexcept>
#include <resolv.h>
#include <algorithm>

#include <iostream>

#include "query.h"

static std::map<std::string, uint16_t> type_map = {
	{ "A",		  1 },
	{ "SOA",	  6 },
	{ "PTR",	 12 },
	{ "MX",		 15 },
	{ "TXT",	 16 },
	{ "AAAA",	 28 },
	{ "SRV",	 33 },
};

uint16_t type_to_number(const std::string& type, bool check_case = true)
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

Query::Query(const std::string& name, const std::string& type)
{
	uint16_t qtype = type_to_number(type);

	int n = res_mkquery(0, name.c_str(), 1, qtype, nullptr, 0, nullptr, buffer.data(), buffer.size());
	if (n < 0) {
		throw std::runtime_error("couldn't parse domain name");
	} else {
		len = n;
	}
}

Query::Query(const Buffer& input, size_t _len)
{
	len = _len;
	std::copy(input.cbegin(), input.cbegin() + len, buffer.begin());
}
