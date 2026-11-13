#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include "IDHANDownloader/parsers/ParserCacheSnapshot.hpp"

namespace idhan::downloader
{

//! JSON strings keep cached values independent of QuickJS realms and runtime threads.
class ParserKeyValueStore
{
  public:

	using Clock = std::chrono::steady_clock;

	ParserKeyValueStore() noexcept = default;

  private:

	struct Entry
	{
		std::string json;
		std::chrono::seconds lifetime;
		Clock::time_point expires;
		std::uint64_t hits {};
	};

	using Key = std::pair< std::string, std::string >;
	std::mutex m_mutex {};
	std::map< Key, Entry > m_entries {};
	Clock::time_point m_next_sweep {};
	std::uint64_t m_hits {};
	std::uint64_t m_misses {};

	void sweep( const Clock::time_point now )
	{
		if ( now < m_next_sweep ) return;

		std::erase_if( m_entries, [ now ]( const auto& item ) { return item.second.expires <= now; } );
		m_next_sweep = now + std::chrono::minutes { 1 };
	}

  public:

	[[nodiscard]] std::optional< std::string > get(
		std::string parser,
		std::string key,
		const Clock::time_point now = Clock::now() )
	{
		const std::scoped_lock lock { m_mutex };
		sweep( now );
		const auto found { m_entries.find( Key { std::move( parser ), std::move( key ) } ) };
		if ( found == m_entries.end() )
		{
			++m_misses;
			return std::nullopt;
		}

		if ( found->second.expires <= now )
		{
			m_entries.erase( found );
			++m_misses;
			return std::nullopt;
		}

		++m_hits;
		++found->second.hits;
		found->second.expires = now + found->second.lifetime;
		return found->second.json;
	}

	void set(
		std::string parser,
		std::string key,
		std::string json,
		const std::chrono::seconds lifetime = std::chrono::days { 1 },
		const Clock::time_point now = Clock::now() )
	{
		if ( lifetime.count() <= 0 ) throw std::invalid_argument( "KV lifetime must be positive" );

		if ( lifetime > std::chrono::duration_cast< std::chrono::seconds >( Clock::time_point::max() - now ) )
			throw std::invalid_argument( "KV lifetime is too large" );

		const std::scoped_lock lock { m_mutex };
		sweep( now );
		const Key scoped_key { std::move( parser ), std::move( key ) };
		const auto found { m_entries.find( scoped_key ) };
		const auto hits { found != m_entries.end() && found->second.expires > now ? found->second.hits : 0 };
		m_entries.insert_or_assign( scoped_key, Entry { std::move( json ), lifetime, now + lifetime, hits } );
	}

	[[nodiscard]] ParserCacheSnapshot snapshot(
		const std::size_t limit = 100,
		const Clock::time_point now = Clock::now() )
	{
		ParserCacheSnapshot result;
		{
			const std::scoped_lock lock { m_mutex };
			std::erase_if( m_entries, [ now ]( const auto& item ) { return item.second.expires <= now; } );
			result.hits = m_hits;
			result.misses = m_misses;
			result.entry_count = m_entries.size();
			result.entries.reserve( m_entries.size() );
			for ( const auto& [ key, entry ] : m_entries )
			{
				result.entries.push_back(
					{ key.first,
					  key.second,
					  entry.hits,
					  entry.lifetime.count(),
					  std::chrono::ceil< std::chrono::seconds >( entry.expires - now ).count() } );
			}
		}

		const auto reads { static_cast< double >( result.hits ) + static_cast< double >( result.misses ) };
		if ( reads > 0 ) result.hit_rate = static_cast< double >( result.hits ) / reads;

		std::ranges::sort(
			result.entries,
			[]( const ParserCacheEntry& left, const ParserCacheEntry& right )
			{
				if ( left.hits != right.hits ) return left.hits > right.hits;

				return std::tie( left.parser, left.key ) < std::tie( right.parser, right.key );
			} );
		if ( result.entries.size() > limit ) result.entries.resize( limit );

		return result;
	}

	bool erase( std::string parser, std::string key )
	{
		const std::scoped_lock lock { m_mutex };
		sweep( Clock::now() );
		return m_entries.erase( Key { std::move( parser ), std::move( key ) } ) != 0;
	}
};

} // namespace idhan::downloader
