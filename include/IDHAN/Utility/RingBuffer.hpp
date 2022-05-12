//
// Created by kj16609 on 5/10/22.
//

#ifndef IDHAN_RINGBUFFER_HPP
#define IDHAN_RINGBUFFER_HPP

#include <array>
#include <functional>
#include <semaphore>
#include <stdexcept>
#include <vector>
#include <optional>

template < typename T, unsigned int num >
requires std::is_trivial_v<T> && std::is_assignable_v<T&, T>
class ringBuffer
{
private:
	std::array<T, num> buffer{};

	T* read{ buffer.begin() };
	T* write{ buffer.begin() };

	std::mutex readLock{};
	std::mutex writeLock{};

	std::counting_semaphore<num> writeCounter{ num };
	std::counting_semaphore<num> readCounter{ 0 };

public:
	size_t size()
	{
		return buffer.size();
	}

	template < typename Time >
	std::optional<T> getNext_for( Time time )
	{
		std::unique_lock<std::mutex> lock( readLock );

		if ( !readCounter.template try_acquire_for( time ))
		{
			return std::nullopt;
		}
		else
		{
			auto ret = read;
			read++;
			if ( read > buffer.end())
			{
				read = buffer.data();
			}
			writeCounter.release();
			return *ret;
		}
	}

	T& getNext()
	{
		std::lock_guard<std::mutex> lock( readLock );

		readCounter.acquire();

		auto ret = read;
		read++;

		if ( read > buffer.end())
		{
			// Out of bounds
			read = buffer.data();
		}

		writeCounter.release();
		return *ret;
	}

	template < typename Time >
	std::optional<T*> pushNext( T var, Time time = std::chrono::milliseconds( 5 ))
	{
		std::lock_guard<std::mutex> lock( writeLock );

		if ( !writeCounter.template try_acquire_for( time ))
		{
			return std::nullopt;
		}

		if ( write > buffer.end())
		{
			write = buffer.data();
		}

		*write = var;

		auto temp = write;

		write++;

		readCounter.release();
		return temp;
	}
};

#endif //IDHAN_RINGBUFFER_HPP
