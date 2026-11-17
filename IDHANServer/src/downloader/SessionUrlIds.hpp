#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::downloader
{
//! Hands out per-session URL ids from memory. The server is the only writer of URL rows, so loading
//! each session's highest id once at startup keeps the counters ahead of the table.
class SessionUrlIds
{
	std::mutex m_mutex {};
	//! Node-based, so a counter stays in place while other sessions are added.
	std::unordered_map< DownloadSessionID, std::atomic< DownloadSessionUrlID > > m_next {};

	std::atomic< DownloadSessionUrlID >& counter( DownloadSessionID session_id );

  public:

	static SessionUrlIds& instance();

	//! Must finish before any URL is inserted.
	runtime::Task< void > load( db::DBContext& db );
	void create( DownloadSessionID session_id );
	void ensureNext( DownloadSessionID session_id, DownloadSessionUrlID minimum );
	void remove( DownloadSessionID session_id );
	//! Returns the first of count consecutive ids.
	[[nodiscard]] DownloadSessionUrlID reserve( DownloadSessionID session_id, std::size_t count = 1 );
};
} // namespace idhan::downloader
