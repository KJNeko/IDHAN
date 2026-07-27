//
// Client-side tag resolution cache.
//

#include "TagCache.hpp"

#include <algorithm>

namespace idhan
{

TagCache::TagCache( const std::size_t byte_budget ) : m_budget( byte_budget )
{}

std::size_t TagCache::entryCost( const Key& key )
{
	constexpr std::size_t per_entry_overhead { 180 };
	return per_entry_overhead + 2 * ( key.first.size() + key.second.size() );
}

void TagCache::touch( const std::list< Entry >::iterator it )
{
	// splice within the same list preserves the node (and any iterators to it), just relinks it
	m_lru.splice( m_lru.begin(), m_lru, it );
}

std::vector< std::optional< TagID > > TagCache::getMany( const std::vector< Key >& keys )
{
	std::vector< std::optional< TagID > > out( keys.size() );

	const std::lock_guard< std::mutex > lock { m_mutex };
	for ( std::size_t i = 0; i < keys.size(); ++i )
	{
		if ( const auto it = m_index.find( keys[ i ] ); it != m_index.end() )
		{
			touch( it->second );
			out[ i ] = it->second->id;
		}
	}

	return out;
}

void TagCache::insertLocked( const Key& key, const TagID id )
{
	if ( const auto it = m_index.find( key ); it != m_index.end() )
	{
		it->second->id = id;
		touch( it->second );
		return;
	}

	m_lru.push_front( Entry { key, id } );
	m_index.emplace( key, m_lru.begin() );
	m_bytes += entryCost( key );
}

void TagCache::putMany( const std::vector< Key >& keys, const std::vector< TagID >& ids )
{
	const std::lock_guard< std::mutex > lock { m_mutex };

	const std::size_t n { std::min( keys.size(), ids.size() ) };
	for ( std::size_t i = 0; i < n; ++i ) insertLocked( keys[ i ], ids[ i ] );

	evictToBudget();
}

void TagCache::evictToBudget()
{
	while ( m_bytes > m_budget && !m_lru.empty() )
	{
		const Entry& victim { m_lru.back() };
		m_bytes -= entryCost( victim.key );
		m_index.erase( victim.key );
		m_lru.pop_back();
	}
}

void TagCache::setBudget( const std::size_t byte_budget )
{
	const std::lock_guard< std::mutex > lock { m_mutex };
	m_budget = byte_budget;
	evictToBudget();
}

std::size_t TagCache::byteUsage() const
{
	const std::lock_guard< std::mutex > lock { m_mutex };
	return m_bytes;
}

std::size_t TagCache::count() const
{
	const std::lock_guard< std::mutex > lock { m_mutex };
	return m_index.size();
}

} // namespace idhan
