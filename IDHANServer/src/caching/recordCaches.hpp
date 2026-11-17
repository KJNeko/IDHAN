#pragma once
#include <cstddef>
#include <list>
#include <mutex>
#include <unordered_map>

#include "IDHANTypes.hpp"

namespace idhan::caching
{

//! LRU membership of record_ids known to exist (append-only, positives only).
class RecordExistsCache
{
	static constexpr std::size_t MAX_ENTRIES{ 512 * 1024 };

	std::list< RecordID > m_recency{};
	std::unordered_map< RecordID, std::list< RecordID >::iterator > m_positions{};
	std::mutex m_mutex {};

	bool touch( const RecordID record_id )
	{
		const auto it{ m_positions.find( record_id ) };
		if ( it == m_positions.end() )
			return false;

		m_recency.splice( m_recency.begin(), m_recency, it->second );
		return true;
	}


public:
	RecordExistsCache() { m_positions.reserve( MAX_ENTRIES ); }

	[[nodiscard]] bool contains( const RecordID record_id )
	{
		const std::lock_guard< std::mutex > lock { m_mutex };
		return touch( record_id );
	}

	void insert( const RecordID record_id )
	{
		const std::lock_guard< std::mutex > lock { m_mutex };
		if ( touch( record_id ) )
			return;

		m_recency.push_front( record_id );
		m_positions.emplace( record_id, m_recency.begin() );
		if ( m_recency.size() <= MAX_ENTRIES )
			return;

		m_positions.erase( m_recency.back() );
		m_recency.pop_back();
	}
};

inline RecordExistsCache& recordExistsCache()
{
	static RecordExistsCache instance;
	return instance;
}

} // namespace idhan::caching
