#ifndef __query_h
#define __query_h

#include <string>
#include <array>

class Query {

private:
	std::array<uint8_t, 180>	buffer;
	size_t						len;

public:
	Query(const std::string& name, const std::string& qtype);

};

#endif // __query_h
