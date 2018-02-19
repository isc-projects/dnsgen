#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cerrno>
#include <algorithm>

#include "datafile.h"

void Datafile::read(const std::string& filename)
{
	std::deque<Query> list;
	std::string line;
	size_t line_no = 0;

	std::ifstream file(filename);
	if (!file) {
		throw std::system_error(errno, std::system_category(), "opening datafile");
	}

	std::string name, type;

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

	std::cout << "data file loaded with " << std::to_string(list.size()) << " queries" << std::endl;
	queries.assign(list);
}

const Query& Datafile::next() {
	return queries.next();
}
