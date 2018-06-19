#include <iostream>
#include <iomanip>
#include "timer.h"

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

uint64_t BenchmarkTimer::current_id = 0;

BenchmarkTimer::BenchmarkTimer(const std::string& _name, clockid_t _clock_id)
{
	name = _name;
	timer_id = current_id++;
	clock_id = _clock_id;
	clock_gettime(clock_id, &start);
}

BenchmarkTimer::~BenchmarkTimer()
{
	write(std::cerr);
	std::cerr << std::endl;
}

timespec BenchmarkTimer::elapsed() const
{
	timespec now;
	clock_gettime(clock_id, &now);
	return now - start;
}

std::ostream& BenchmarkTimer::write(std::ostream& os) const
{
	auto t = elapsed();

	using namespace std;
	ios init(nullptr);
	init.copyfmt(os);
	os << "timer " << setw(4) << timer_id << " - " << setw(20) << left << name << ": " << t;
	os.copyfmt(init);

	return os;
}

std::ostream& operator<<(std::ostream& os, const BenchmarkTimer& timer)
{
	return timer.write(os);
}
