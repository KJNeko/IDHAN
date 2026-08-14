#pragma once
#include <mutex>
#include <optional>

#include "IDHANTypes.hpp"
#include "caching/LRUCache.hpp"
#include "crypto/SHA256.hpp"

namespace idhan::caching
{

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
