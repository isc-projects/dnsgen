#include <iostream>
#include <iomanip>

static const long ns_per_s = 1000000000UL;

std::ostream& operator<<(std::ostream& os, const timespec& ts)
{
	using namespace std;
	ios init(nullptr);
	init.copyfmt(os);
	os << ts.tv_sec << "." << setw(9) << setfill('0') << ts.tv_nsec;
	os.copyfmt(init);
	return os;
}

timespec operator-(const timespec& a, const timespec& b)
{
	timespec res;

	if (a.tv_nsec < b.tv_nsec) {
		res.tv_sec = a.tv_sec - b.tv_sec - 1;
		res.tv_nsec = ns_per_s + a.tv_nsec - b.tv_nsec;
	}  else {
		res.tv_sec = a.tv_sec - b.tv_sec;
		res.tv_nsec = a.tv_nsec - b.tv_nsec;
	}

	return res;
}

timespec operator+(const timespec& a, const timespec& b)
{
	timespec res;

	res.tv_sec = a.tv_sec + b.tv_sec;
	res.tv_nsec = a.tv_nsec + b.tv_nsec;
	while (res.tv_nsec > ns_per_s) {
		res.tv_sec += 1;
		res.tv_nsec -= ns_per_s;
	}

	return res;
}

timespec operator+(const timespec& a, const uint64_t ns)
{
	auto div = ldiv(ns, ns_per_s);
	timespec delta = { div.quot, div.rem };;
	return a + delta;
}
