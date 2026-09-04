#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "SessionObserver.hpp"
#include "SessionSnapshot.hpp"

namespace idhan::downloader
{
class DownloaderContext;

struct SessionOptions
{
	std::string root_url {};
	SessionObserver* observer {};
	//! In-flight HTTP requests allowed before the loop stops starting new scripts. 0 takes the context default.
	std::size_t inflight_requests {};
	//! Returned unchanged with each ImportRequest.
	std::uint64_t host_tag {};
};

//! State for one crawl; execution occurs on the shared script runner.
class SessionContext
{
  public:

	class Impl;

  private:

	std::unique_ptr< Impl > m_impl;

	friend class DownloaderContext;
	friend class ScriptRunner;
	explicit SessionContext( std::unique_ptr< Impl > impl );

	[[nodiscard]] Impl& impl() const { return *m_impl; }

  public:

	SessionContext( const SessionContext& ) = delete;
	SessionContext& operator=( const SessionContext& ) = delete;
	~SessionContext();

	//! Reserves the work ID before making the URL visible to workers.
	[[nodiscard]] std::expected< WorkID, std::string > submit(
		std::string url,
		std::optional< WorkID > parent = std::nullopt,
		std::function< void( WorkID ) > on_reserved = {} );

	//! Cancels queued and active work without blocking.
	void cancel();
	//! Blocks until the session is idle.
	void wait();
	//! Rejects submissions and lets current work drain.
	void close();

	[[nodiscard]] bool idle() const;
	[[nodiscard]] std::size_t outstanding() const;
	[[nodiscard]] const std::string& rootUrl() const;

	//! Returns events after `since`; use the prior event_sequence as the next cursor.
	[[nodiscard]] SessionSnapshot snapshot( std::uint64_t since = 0 ) const;
};

} // namespace idhan::downloader
