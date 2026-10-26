#pragma once
#include <folly/coro/Baton.h>
#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/HTTPHeaders.h>

#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>

#include "idhan/runtime/Task.hpp"

namespace idhan::http
{
class ResponseChannel
{
	const std::size_t m_capacity;
	std::mutex m_mutex;
	std::deque< std::unique_ptr< folly::IOBuf > > m_chunks;
	std::optional< proxygen::HTTPHeaders > m_trailers;
	std::exception_ptr m_error;
	bool m_finished { false };
	bool m_abandoned { false };
	folly::coro::Baton m_readable;
	folly::coro::Baton m_writable { true };

  public:

	explicit ResponseChannel( std::size_t capacity = 2 );
	~ResponseChannel() = default;
	[[nodiscard]] runtime::Task<> push( std::unique_ptr< folly::IOBuf > );
	[[nodiscard]] runtime::Task< std::unique_ptr< folly::IOBuf > > next();
	void finish( proxygen::HTTPHeaders trailers = {} );
	void fail( std::exception_ptr );
	void abandon() noexcept;
	[[nodiscard]] proxygen::HTTPHeaders trailers() const;
};
} // namespace idhan::http
