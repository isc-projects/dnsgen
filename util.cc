#include <system_error>
#include <cerrno>

#include "util.h"

void throw_errno(const std::string& what)
{
	throw std::system_error(errno, std::system_category(), what);
}
