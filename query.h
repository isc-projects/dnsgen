#ifndef __query_h
#define __query_h

#include <string>
#include <vector>
#include <array>

class Query {

public:
	typedef std::array<uint8_t, 180> Buffer;

private:
	Buffer						buffer;
	size_t						len;

public:
	Query(const std::string& name, const std::string& qtype);
	Query(const Buffer& buffer, size_t len);

	size_t size() const {
		return len;
	}

	const uint8_t* const data() const {
		return buffer.data();
	}
};

#endif // __query_h
