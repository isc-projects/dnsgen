/*
 * timer.h
 */

#pragma once

#include <time.h>
#include <iostream>
#include <string>

std::ostream& operator<<(std::ostream& os, const timespec& ts);
timespec operator-(const timespec& a, const timespec& b);
timespec operator+(const timespec& a, const timespec& b);
timespec operator+(const timespec& a, const uint64_t ns);

class BenchmarkTimer {

	static uint64_t		current_id;

	std::string		name;
	uint64_t		timer_id;
	clockid_t		clock_id;
	timespec		start;

public:
	BenchmarkTimer(const std::string& name, clockid_t clock_id = CLOCK_PROCESS_CPUTIME_ID);
	~BenchmarkTimer();

public:
	timespec		elapsed() const;
	std::ostream&		write(std::ostream&) const;

};

std::ostream& operator<<(std::ostream&, const BenchmarkTimer&);
