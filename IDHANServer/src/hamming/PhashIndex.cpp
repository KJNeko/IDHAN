#include "PhashIndex.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

#if defined( __x86_64__ )
#define IDHAN_POPCOUNT_CLONES [[gnu::target_clones( "default", "popcnt", "arch=x86-64-v4" )]]
#else
#define IDHAN_POPCOUNT_CLONES
#endif

namespace idhan::hamming
{
IDHAN_POPCOUNT_CLONES static std::vector< std::size_t > indicesWithin(
	const std::uint64_t probe,
	const std::span< const std::uint64_t > hashes,
	const std::uint8_t max_distance )
{
	std::vector< std::size_t > matches;
	for ( std::size_t index {}; index < hashes.size(); ++index )
		if ( std::popcount( probe ^ hashes[ index ] ) <= max_distance ) matches.push_back( index );
	return matches;
}

PhashIndex::PhashIndex( std::vector< RecordID > records, std::vector< std::uint64_t > hashes ) :
  m_records( std::move( records ) ),
  m_hashes( std::move( hashes ) )
{
	if ( m_records.size() != m_hashes.size() ) throw std::invalid_argument( "Phash index arrays differ in length" );

	if ( !std::ranges::is_sorted( m_records ) ) throw std::invalid_argument( "Phash index records are not sorted" );
}

std::size_t PhashIndex::size() const noexcept
{
	return m_records.size();
}

std::optional< std::uint64_t > PhashIndex::find( const RecordID record ) const
{
	const auto found { std::ranges::lower_bound( m_records, record ) };
	const bool present { found != m_records.end() && *found == record };
	if ( !present ) return std::nullopt;

	return m_hashes[ static_cast< std::size_t >( found - m_records.begin() ) ];
}

void PhashIndex::assign( const RecordID record, const std::optional< std::uint64_t > hash )
{
	const auto found { std::ranges::lower_bound( m_records, record ) };
	const auto position { found - m_records.begin() };
	const bool present { found != m_records.end() && *found == record };
	const bool replaced { present && hash.has_value() };
	if ( replaced )
	{
		m_hashes[ static_cast< std::size_t >( position ) ] = hash.value();
		return;
	}

	if ( present )
	{
		m_records.erase( found );
		m_hashes.erase( m_hashes.begin() + position );
		return;
	}

	if ( !hash.has_value() ) return;

	m_records.insert( found, record );
	m_hashes.insert( m_hashes.begin() + position, hash.value() );
}

std::vector< HammingPair > PhashIndex::pairsWithin(
	const std::span< const RecordID > probes,
	const std::uint8_t max_distance ) const
{
	std::vector< HammingPair > pairs;
	for ( const auto probe : probes )
	{
		const auto probe_hash { find( probe ) };
		if ( !probe_hash.has_value() ) continue;

		for ( const auto index : indicesWithin( probe_hash.value(), m_hashes, max_distance ) )
		{
			const auto other { m_records[ index ] };
			if ( other == probe ) continue;

			const auto distance { static_cast< SmallInt >( std::popcount( probe_hash.value() ^ m_hashes[ index ] ) ) };
			pairs.push_back( { std::min( probe, other ), std::max( probe, other ), distance } );
		}
	}
	std::ranges::sort( pairs );
	const auto duplicates { std::ranges::unique( pairs ) };
	pairs.erase( duplicates.begin(), duplicates.end() );
	return pairs;
}
} // namespace idhan::hamming
