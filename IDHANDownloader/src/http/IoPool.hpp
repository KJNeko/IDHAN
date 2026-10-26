#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "IoThread.hpp"

namespace idhan::downloader
{

class IoPool
{
	std::vector< std::unique_ptr< IoThread > > m_threads {};

  public:

	explicit IoPool( std::size_t count );
	IoPool( const IoPool& ) = delete;
	IoPool& operator=( const IoPool& ) = delete;
	~IoPool();

	[[nodiscard]] IoThread& leastLoaded();

	[[nodiscard]] std::size_t size() const { return m_threads.size(); }

	void stop();

	static void initialiseCurl();
	[[nodiscard]] static bool supportsHttp3();
	[[nodiscard]] static bool supportsHttp2();
};

} // namespace idhan::downloader
