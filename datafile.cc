#include <cstdio>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cerrno>
#include <algorithm>

#include "datafile.h"

void Datafile::read_txt(const std::string& filename)
{
	std::ifstream file(filename);
	if (!file) {
		throw std::system_error(errno, std::system_category(), "opening datafile");
	}

	std::deque<Query> list;
	std::string name, type;
	size_t line_no = 0;

	while (file >> name >> type) {
		line_no++;

		try {
			list.emplace_back(Query(name, type));
		} catch (std::runtime_error &e) {
			std::string error = "reading datafile at line " + std::to_string(line_no)
					+ ": " + e.what();
			throw std::runtime_error(error);
		}
	}

	file.close();

	std::cout << "data file loaded with " << std::to_string(list.size()) << " queries" << std::endl;
	queries.assign(list);
}

void Datafile::read_raw(const std::string& filename)
{
	std::ifstream file(filename, std::ifstream::binary);
	if (!file) {
		throw std::system_error(errno, std::system_category(), "opening datafile");
	}

	std::deque<Query> list;
	Query::Buffer buffer;
	uint16_t len;

	while (file) {
		file.read(reinterpret_cast<char*>(&len), sizeof(len));
		file.read(reinterpret_cast<char*>(buffer.data()), len);
		list.emplace_back(Query(buffer, len));
	}

	file.close();

	std::cout << "raw data file loaded with " << std::to_string(list.size()) << " queries" << std::endl;
	queries.assign(list);
}

void Datafile::write_raw(const std::string& filename)
{
	std::ofstream file(filename, std::ifstream::binary);
	if (!file) {
		throw std::system_error(errno, std::system_category(), "opening datafile");
	}

	for (size_t i = 0, n = queries.count(); i < n; ++i) {
		const auto& query = queries.next();

		uint16_t len = query.size();
		auto data = query.data();

		file.write(reinterpret_cast<const char*>(&len), sizeof(len));
		file.write(reinterpret_cast<const char*>(data), len);
	}

	file.close();
}
