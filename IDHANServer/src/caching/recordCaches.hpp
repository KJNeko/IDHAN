//
// Created by kj16609 on 7/25/26.
//

#pragma once
#include <mutex>
#include <optional>

#include "IDHANTypes.hpp"
#include "caching/LRUCache.hpp"
#include "crypto/SHA256.hpp"

namespace idhan::caching
{

// Process-wide, thread-safe caches over the immutable `records` table. The record_id -> sha256 mapping
// and record existence are append-only (records are never deleted and sha256 is content-addressed), so
// cached entries never go stale.
//
// A single shared instance guarded by a mutex — rather than a thread_local — keeps the cache warm
// across every worker thread and bounds total memory to the byte budget below, instead of
// budget * thread_count with each thread starting cold.
//
// Every operation mutates LRU recency (value()/exists() promote to most-recently-used), so there are
// no pure readers: a plain exclusive mutex is correct and a shared_mutex would buy nothing. Locks are
// released before the caller resumes, so no lock is ever held across a co_await.

inline constexpr std::size_t kRecordCacheBytes { 64ull * 1024 * 1024 }; // 64 MiB per cache

// record_id -> sha256 (immutable, content-addressed).
class RecordSha256Cache
{
	LRUCache< RecordID, SHA256 > m_cache { kRecordCacheBytes };
	std::mutex m_mutex;

  public:

	[[nodiscard]] std::optional< SHA256 > get( const RecordID id )
	{
		const std::lock_guard< std::mutex > lock { m_mutex };
		return m_cache.value( id );
	}

	void put( const RecordID id, const SHA256& sha256 )
	{
		const std::lock_guard< std::mutex > lock { m_mutex };
		m_cache.add( id, sha256 );
	}
};

// Membership of record_ids known to exist (append-only, positives only).
class RecordExistsCache
{
	ValidLRUCache< RecordID > m_cache { kRecordCacheBytes };
	std::mutex m_mutex;

  public:

	[[nodiscard]] bool contains( const RecordID id )
	{
		const std::lock_guard< std::mutex > lock { m_mutex };
		return m_cache.exists( id );
	}

	void insert( const RecordID id )
	{
		const std::lock_guard< std::mutex > lock { m_mutex };
		m_cache.add( id );
	}
};

inline RecordSha256Cache& recordSha256Cache()
{
	static RecordSha256Cache instance;
	return instance;
}

inline RecordExistsCache& recordExistsCache()
{
	static RecordExistsCache instance;
	return instance;
}

} // namespace idhan::caching
