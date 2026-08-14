
#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IDHANTypes.hpp"

namespace idhan
{

//! Default byte budget for the client tag cache (1 GiB).
inline constexpr std::size_t TAG_CACHE_DEFAULT_BUDGET_BYTES { 1024ull * 1024ull * 1024ull };

//! Hash for (namespace, subtag) tag-text pairs, via boost::hash_combine (matches SHA256.hpp).
struct TagPairHash
{
	std::size_t operator()( const std::pair< std::string, std::string >& pair ) const noexcept
	{
		std::size_t seed { 0 };
		seed ^= std::hash< std::string > {}( pair.first ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
		seed ^= std::hash< std::string > {}( pair.second ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
		return seed;
	}
};

//! Thread-safe, byte-budgeted LRU cache mapping a (namespace, subtag) tag to its server-side TagID.
class TagCache
{
  public:

	using Key = std::pair< std::string, std::string >;

	explicit TagCache( std::size_t byte_budget = TAG_CACHE_DEFAULT_BUDGET_BYTES );

	//! Batch lookup. result[i] holds the id for keys[i], or std::nullopt on a miss. Hits are marked
	//! most-recently-used.
	[[nodiscard]] std::vector< std::optional< TagID > > getMany( const std::vector< Key >& keys );

	//! Batch insert of resolved tags (keys[i] -> ids[i]), evicting to the budget once afterwards.
	void putMany( const std::vector< Key >& keys, const std::vector< TagID >& ids );

	//! Change the byte budget, evicting immediately if the cache is now over it.
	void setBudget( std::size_t byte_budget );

	[[nodiscard]] std::size_t byteUsage() const;
	[[nodiscard]] std::size_t count() const;

  private:

	struct Entry
	{
		Key key;
		TagID id;
	};

	[[nodiscard]] static std::size_t entryCost( const Key& key );

	//! Move an existing node to the most-recently-used position. Caller holds m_mutex.
	void touch( std::list< Entry >::iterator it );

	//! Insert or update a single entry. Caller holds m_mutex.
	void insertLocked( const Key& key, TagID id );

	//! Evict least-recently-used entries until at or below budget. Caller holds m_mutex.
	void evictToBudget();

	mutable std::mutex m_mutex;
	std::list< Entry > m_lru {};
	std::unordered_map< Key, std::list< Entry >::iterator, TagPairHash > m_index {};
	std::size_t m_bytes { 0 };
	std::size_t m_budget;
};

} // namespace idhan
