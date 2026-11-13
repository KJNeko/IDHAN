#include "Set.hpp"

#include <algorithm>
#include <concepts>
#include <numeric>
#include <random>
#include <stdexcept>

namespace idhan::search
{

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
	if ( lhs.m_hashes || rhs.m_hashes ) out.m_hashes.emplace();

	//TODO: Rewrite
	std::visit(
		[ & ]< typename T0, typename T1 >( const T0& lhs_keys, const T1& rhs_keys )
		{
			using Left = std::decay_t< T0 >;
			using Right = std::decay_t< T1 >;

			if constexpr ( !std::same_as< Left, Right > )
			{
				throw std::logic_error( "SearchBuilder: combined two Sets built for different sort types" );
			}
			else
			{
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
						if ( op != MergeOp::Intersect ) emit( lhs, lhs_keys, i );
						++i;
					}
					else if ( compositeLess( rhs_keys, rhs.m_ids, j, lhs_keys, lhs.m_ids, i ) )
					{
						if ( op == MergeOp::Union || op == MergeOp::SymmetricDifference ) emit( rhs, rhs_keys, j );
						++j;
					}
					else
					{
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
	if ( !m_inverted && other.m_inverted ) return merge( *this, other, MergeOp::Difference, false );
	if ( m_inverted && !other.m_inverted ) return merge( other, *this, MergeOp::Difference, false );

	return merge( *this, other, MergeOp::Union, true );
}

Set Set::operator|( const Set& other ) const
{
	if ( !m_inverted && !other.m_inverted ) return merge( *this, other, MergeOp::Union, false );
	if ( !m_inverted && other.m_inverted ) return merge( other, *this, MergeOp::Difference, true );
	if ( m_inverted && !other.m_inverted ) return merge( *this, other, MergeOp::Difference, true );

	return merge( *this, other, MergeOp::Intersect, true );
}

Set Set::operator^( const Set& other ) const
{
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
		[]< typename T0 >( T0& held )
		{
			if constexpr ( !std::same_as< std::decay_t< T0 >, std::monostate > )
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
