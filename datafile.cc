#include <cstdio>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cerrno>
#include <algorithm>

#include <arpa/inet.h>		// for ntohs() etc

#include "datafile.h"
#include "util.h"

void Datafile::read_txt(const std::string& filename)
{
	std::ifstream file(filename);
	if (!file) {
		throw_errno("opening datafile");
	}

	storage_t list;
	std::string name, type;
	size_t line_no = 0;

	while (file >> name >> type) {
		line_no++;

		try {
			list.emplace_back(Query(name, type));
		} catch (std::runtime_error &e) {
			std::string error = "reading datafile at line "
					+ std::to_string(line_no)
					+ ": " + e.what();
			throw_errno(error);
		}
	}

	file.close();

	std::swap(queries, list);
}

void Datafile::read_raw(const std::string& filename)
{
	std::ifstream file(filename, std::ifstream::binary);
	if (!file) {
		throw_errno("opening datafile");
	}

	storage_t list;
	Query::Buffer buffer;
	uint16_t len;

	while (file) {
		if (file.read(reinterpret_cast<char*>(&len), sizeof(len))) {

			len = ntohs(len);			// swap to host order
			if (len > buffer.size()) {
				std::cerr << list.size() << std::endl;
				throw std::runtime_error("raw file record size exceeds maximum");
			}

			if (file.read(reinterpret_cast<char*>(buffer.data()), len)) {
				list.emplace_back(Query(buffer, len));
			}
		}
	}

	file.close();

	std::swap(queries, list);
}

void Datafile::write_raw(const std::string& filename)
{
	std::ofstream file(filename, std::ifstream::binary);
	if (!file) {
		throw_errno("opening datafile");
	}

	for (const auto& query: queries) {
		uint16_t len = htons(query.size());	// big-endian
		file.write(reinterpret_cast<const char*>(&len), sizeof(len));
		file.write(reinterpret_cast<const char*>(query.data()), query.size());
	}

	file.close();
}
