#pragma once

#include <cstdint>
#include <cassert>
#include <sys/socket.h>		// for iovec

class Buffer {

protected:

	uint8_t*		_base = nullptr;
	size_t			_size = 0;
	size_t			_position = 0;

protected:
	uint8_t* allocate(size_t n);
	uint8_t& operator[](size_t x) const;

protected:
	Buffer(uint8_t* base, size_t size);

public:
	size_t size() const;
	size_t position() const;
	size_t available() const;
	void reset();
};

class ReadBuffer : public Buffer {

public:
	ReadBuffer(const uint8_t* base, size_t size);

public:
	const void* base() const;

	template<typename T> const T& read();
	template<typename T> const T* read(size_t n);

	const uint8_t& operator[](size_t x) const;
	operator iovec() const;
};

class WriteBuffer : public Buffer {

public:
	WriteBuffer(uint8_t* base, size_t size);

public:
	void* base() const;

	template<typename T> T& reserve();
	template<typename T> T* reserve(size_t n);

	template<typename T> T& write(const T& v);

	uint8_t& operator[](size_t x) const;
	operator iovec() const;
};

//---------------------------------------------------------------------

inline Buffer::Buffer(uint8_t* base, size_t size) : _base(base), _size(size)
{
	assert(base);
}

inline ReadBuffer::ReadBuffer(const uint8_t* base, size_t size)
	: Buffer(const_cast<uint8_t*>(base), size)
{
}

inline WriteBuffer::WriteBuffer(uint8_t* base, size_t size)
	: Buffer(base, size)
{
}

//---------------------------------------------------------------------

inline void Buffer::reset() {
	_position = 0;
};

inline size_t Buffer::size() const {
	return _size;
}

inline size_t Buffer::position() const {
	return _position;
}

inline size_t Buffer::available() const {
	assert(_size >= _position);
	return _size - _position;
}

inline uint8_t* Buffer::allocate(size_t n) {
	assert(_base);
	assert(available() >= n);
	auto p = _base + _position;
	_position += n;
	return p;
}

//---------------------------------------------------------------------

inline const void* ReadBuffer::base() const {
	return _base;
}

inline void* WriteBuffer::base() const {
	return _base;
}

//---------------------------------------------------------------------

inline uint8_t& Buffer::operator[](size_t x) const {
	assert(_base);
	assert(x < _size);
	return _base[x];
}

inline const uint8_t& ReadBuffer::operator[](size_t x) const {
	return Buffer::operator[](x);
}

inline uint8_t& WriteBuffer::operator[](size_t x) const {
	return Buffer::operator[](x);
}

//---------------------------------------------------------------------

template<typename T>
const T* ReadBuffer::read(size_t n) {
	return reinterpret_cast<const T*>(allocate(n * sizeof(T)));
}

template<typename T>
const T& ReadBuffer::read() {
	return *read<T>(1);
}

//---------------------------------------------------------------------

template<typename T>
T* WriteBuffer::reserve(size_t n) {
	return reinterpret_cast<T*>(allocate(n * sizeof(T)));
}

template<typename T>
T& WriteBuffer::reserve() {
	return *reserve<T>(1);
}

template<typename T>
T& WriteBuffer::write(const T& v) {
	auto p = reserve<T>(1);
	*p = v;
	return *p;
}

//---------------------------------------------------------------------

inline ReadBuffer::operator iovec() const
{
	return iovec { _base, _size };
}

inline WriteBuffer::operator iovec() const
{
	return iovec { _base, _position };
}

//---------------------------------------------------------------------
