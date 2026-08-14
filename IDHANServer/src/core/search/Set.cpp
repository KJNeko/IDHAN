#include "Set.hpp"

#include <algorithm>
#include <concepts>
#include <numeric>
#include <random>
#include <stdexcept>

namespace idhan::search
{

//! The composite (key, record_id) ordering every Set is held in. For a keyless column all keys
//! compare equal, so the composite degenerates to record_id alone -- which is exactly the order
//! a RANDOM-sorted fetch produces.
template < typename Column >
bool compositeLess(
	const Column& lhs_keys,
	const std::vector< RecordID >& lhs_ids,
	const std::size_t lhs_index,
	const Column& rhs_keys,
	const std::vector< RecordID >& rhs_ids,
	const std::size_t rhs_index )
{
	if constexpr ( !std::same_as< Column, std::monostate > )
	{
		if ( lhs_keys[ lhs_index ] < rhs_keys[ rhs_index ] ) return true;
		if ( rhs_keys[ rhs_index ] < lhs_keys[ lhs_index ] ) return false;
	}

	return lhs_ids[ lhs_index ] < rhs_ids[ rhs_index ];
}

//! Rebuilds \p values in the order \p order names. Used to apply one permutation across every
//! parallel array so they cannot drift apart.
template < typename T >
void applyPermutation( std::vector< T >& values, const std::vector< std::size_t >& order )
{
	std::vector< T > permuted {};
	permuted.reserve( values.size() );
	for ( const auto index : order ) permuted.push_back( values[ index ] );
	values = std::move( permuted );
}

Set::Set(
	std::vector< RecordID > ids,
	SortKeyColumn keys,
	std::optional< std::vector< SHA256 > > hashes,
	const bool inverted ) :
  m_ids( std::move( ids ) ),
  m_keys( std::move( keys ) ),
  m_hashes( std::move( hashes ) ),
  m_inverted( inverted )
{}

Set Set::emptyOf( const SortKeyType type )
{
	Set out {};
	out.m_keys = emptyColumn( type );
	return out;
}

Set Set::merge( const Set& lhs, const Set& rhs, const MergeOp op, const bool inverted )
{
	Set out {};
	out.m_inverted = inverted;
	out.m_keys = emptyColumnLike( lhs.m_keys );
	// Both operands come from the same query and so agree on whether hashes were requested; the ||
	// is defensive rather than meaningful.
	if ( lhs.m_hashes || rhs.m_hashes ) out.m_hashes.emplace();

	std::visit(
		[ & ]( const auto& lhs_keys, const auto& rhs_keys )
		{
			using Left = std::decay_t< decltype( lhs_keys ) >;
			using Right = std::decay_t< decltype( rhs_keys ) >;

			if constexpr ( !std::same_as< Left, Right > )
			{
				// A single search has one sort type throughout, so this is a programming error
				// rather than bad input -- but silently producing a mis-ordered Set would surface
				// much later as wrongly ordered results, so it is worth failing loudly.
				throw std::logic_error( "SearchBuilder: combined two Sets built for different sort types" );
			}
			else
			{
				// unused when the alternative is monostate, where every reference to it sits in a
				// discarded if-constexpr branch
				[[maybe_unused]] auto& out_keys { std::get< Left >( out.m_keys ) };

				const std::size_t reserve_hint {
					op == MergeOp::Intersect  ? std::min( lhs.m_ids.size(), rhs.m_ids.size() ) :
					op == MergeOp::Difference ? lhs.m_ids.size() :
												lhs.m_ids.size() + rhs.m_ids.size()
				};
				out.m_ids.reserve( reserve_hint );
				if ( out.m_hashes ) out.m_hashes->reserve( reserve_hint );
				if constexpr ( !std::same_as< Left, std::monostate > ) out_keys.reserve( reserve_hint );

				const auto emit = [ & ]( const Set& from, const Left& keys, const std::size_t index )
				{
					out.m_ids.push_back( from.m_ids[ index ] );
					if constexpr ( !std::same_as< Left, std::monostate > ) out_keys.push_back( keys[ index ] );
					if ( out.m_hashes && from.m_hashes ) out.m_hashes->push_back( ( *from.m_hashes )[ index ] );
				};

				std::size_t i { 0 };
				std::size_t j { 0 };

				while ( i < lhs.m_ids.size() && j < rhs.m_ids.size() )
				{
					if ( compositeLess( lhs_keys, lhs.m_ids, i, rhs_keys, rhs.m_ids, j ) )
					{
						// present only in the left operand
						if ( op != MergeOp::Intersect ) emit( lhs, lhs_keys, i );
						++i;
					}
					else if ( compositeLess( rhs_keys, rhs.m_ids, j, lhs_keys, lhs.m_ids, i ) )
					{
						// present only in the right operand
						if ( op == MergeOp::Union || op == MergeOp::SymmetricDifference ) emit( rhs, rhs_keys, j );
						++j;
					}
					else
					{
						// present in both
						if ( op == MergeOp::Intersect || op == MergeOp::Union ) emit( lhs, lhs_keys, i );
						++i;
						++j;
					}
				}

				while ( i < lhs.m_ids.size() )
				{
					if ( op != MergeOp::Intersect ) emit( lhs, lhs_keys, i );
					++i;
				}

				while ( j < rhs.m_ids.size() )
				{
					if ( op == MergeOp::Union || op == MergeOp::SymmetricDifference ) emit( rhs, rhs_keys, j );
					++j;
				}
			}
		},
		lhs.m_keys,
		rhs.m_keys );

	return out;
}

Set Set::operator&( const Set& other ) const
{
	if ( !m_inverted && !other.m_inverted ) return merge( *this, other, MergeOp::Intersect, false );
	//  A & ~B  ->  A \ B
	if ( !m_inverted && other.m_inverted ) return merge( *this, other, MergeOp::Difference, false );
	// ~A &  B  ->  B \ A
	if ( m_inverted && !other.m_inverted ) return merge( other, *this, MergeOp::Difference, false );
	// ~A & ~B  ->  ~(A | B)
	return merge( *this, other, MergeOp::Union, true );
}

Set Set::operator|( const Set& other ) const
{
	if ( !m_inverted && !other.m_inverted ) return merge( *this, other, MergeOp::Union, false );
	//  A | ~B  ->  ~(B \ A)
	if ( !m_inverted && other.m_inverted ) return merge( other, *this, MergeOp::Difference, true );
	// ~A |  B  ->  ~(A \ B)
	if ( m_inverted && !other.m_inverted ) return merge( *this, other, MergeOp::Difference, true );
	// ~A | ~B  ->  ~(A & B)
	return merge( *this, other, MergeOp::Intersect, true );
}

Set Set::operator^( const Set& other ) const
{
	// A △ ~B == ~(A △ B): negating either operand negates the result, and negating both cancels.
	return merge( *this, other, MergeOp::SymmetricDifference, m_inverted != other.m_inverted );
}

Set Set::operator~() const &
{
	Set out { *this };
	out.m_inverted = !out.m_inverted;
	return out;
}

Set Set::operator~() &&
{
	m_inverted = !m_inverted;
	return std::move( *this );
}

void Set::reverse()
{
	std::ranges::reverse( m_ids );
	std::visit(
		[]( auto& held )
		{
			if constexpr ( !std::same_as< std::decay_t< decltype( held ) >, std::monostate > )
				std::ranges::reverse( held );
		},
		m_keys );
	if ( m_hashes ) std::ranges::reverse( *m_hashes );
}

void Set::shuffle()
{
	if ( m_ids.size() < 2 ) return;

	std::vector< std::size_t > order( m_ids.size() );
	std::iota( order.begin(), order.end(), std::size_t { 0 } );

	std::random_device seed_source {};
	std::mt19937_64 generator { seed_source() };
	std::ranges::shuffle( order, generator );

	applyPermutation( m_ids, order );
	if ( m_hashes ) applyPermutation( *m_hashes, order );
	std::visit(
		[ &order ]( auto& held )
		{
			if constexpr ( !std::same_as< std::decay_t< decltype( held ) >, std::monostate > )
				applyPermutation( held, order );
		},
		m_keys );
}

void Set::slice( const std::size_t offset, const std::optional< std::size_t > limit )
{
	if ( offset >= m_ids.size() )
	{
		m_ids.clear();
		m_keys = emptyColumnLike( m_keys );
		if ( m_hashes ) m_hashes->clear();
		return;
	}

	// computed as a remaining-count rather than offset + limit, which would overflow on a caller
	// passing a limit near SIZE_MAX
	const std::size_t remaining { m_ids.size() - offset };
	const std::size_t kept { limit ? std::min( remaining, *limit ) : remaining };
	const std::size_t end { offset + kept };

	const auto trim = [ offset, end ]( auto& values )
	{
		values.erase( values.begin() + static_cast< std::ptrdiff_t >( end ), values.end() );
		values.erase( values.begin(), values.begin() + static_cast< std::ptrdiff_t >( offset ) );
	};

	trim( m_ids );
	if ( m_hashes ) trim( *m_hashes );
	std::visit(
		[ &trim ]( auto& held )
		{
			if constexpr ( !std::same_as< std::decay_t< decltype( held ) >, std::monostate > ) trim( held );
		},
		m_keys );
}

} // namespace idhan::search
