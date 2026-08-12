#include "ptr/flatten/TagUsageSet.hpp"

#include <bit>

namespace idhan::hydrus::ptr
{

TagUsageSet::TagUsageSet( const std::uint32_t capacity ) :
  m_capacity( capacity ),
  // std::atomic is neither copyable nor movable, so the vector can only ever be sized here. Value
  // initialisation zeroes every word, which is the empty set.
  m_words( ( static_cast< std::size_t >( capacity ) + BITS_PER_WORD - 1 ) / BITS_PER_WORD )
{}

void TagUsageSet::mark( const std::uint32_t tag_id ) noexcept
{
	if ( tag_id >= m_capacity ) return;

	const auto word = static_cast< std::size_t >( tag_id ) / BITS_PER_WORD;
	const auto bit = std::uint64_t { 1 } << ( tag_id % BITS_PER_WORD );

	// Relaxed: nothing is published through this set. Every reader calls count() only once the
	// marking threads have joined, and that join is the synchronisation.
	m_words[ word ].fetch_or( bit, std::memory_order_relaxed );
}

std::uint64_t TagUsageSet::count() const noexcept
{
	std::uint64_t total { 0 };
	for ( const auto& word : m_words )
		total += static_cast< std::uint64_t >( std::popcount( word.load( std::memory_order_relaxed ) ) );
	return total;
}

} // namespace idhan::hydrus::ptr
