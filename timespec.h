/*
 * timespec.h
 */

#ifndef __timespec_h
#define __timespec_h

#include <iostream>

std::ostream& operator<<(std::ostream& os, const timespec& ts);
timespec operator-(const timespec& a, const timespec& b);
timespec operator+(const timespec& a, const timespec& b);
timespec operator+(const timespec& a, const uint64_t ns);

#endif // __timespec_h
