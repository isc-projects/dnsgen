/*
 * ring.h
 */

#ifndef __ring_h
#define __ring_h

#include <atomic>
#include <deque>

template<typename T> class Ring {

private:
	std::deque<T>			contents;
	std::atomic<size_t>		current;
	size_t					_count = 0;

public:
	size_t count() const { return _count; }

public:
	void assign(std::deque<T>& _contents) {
		std::swap(contents, _contents);
		_count = contents.size();
	}

public:
	const T& next() {	// via https://stackoverflow.com/questions/33554255/

		size_t current_val = current++;
		size_t index = current_val % _count;

		size_t expected_val = current;
		size_t desired_val = expected_val % _count;

		while (!current.compare_exchange_weak(expected_val, desired_val, std::memory_order_relaxed)) {
			desired_val = expected_val % _count;
		} 

		return contents[index];
	}
};

#endif // __ring_h
