#include <system_error>
#include <cerrno>

#include "util.h"

//
// converts a POSIX errno into a runtime exception
//
void throw_errno(const std::string& what)
{
	throw std::system_error(errno, std::system_category(), what);
}
