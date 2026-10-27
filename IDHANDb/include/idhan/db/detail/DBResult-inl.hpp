#pragma once

#include <utility>

#include "../DBBindings.hpp"

namespace idhan::db
{
namespace detail
{
template < typename T >
void validateColumn( PGresult* result, std::size_t column )
{
	const auto actual { PQftype( result, static_cast< int >( column ) ) };
	const auto expected { DBCodec< T >::oid() };
	if ( actual != expected ) throw DBDecodeError( "Column OID mismatch", column, expected, actual );

	if ( PQfformat( result, static_cast< int >( column ) ) != 1 )
		throw DBDecodeError( "Typed rows require binary results", column, expected, actual );
}

template < typename T >
T decodeColumn( PGresult* result, int row, int column )
{
	const auto expected { DBCodec< T >::oid() };
	const auto actual { PQftype( result, column ) };
	if ( PQgetisnull( result, row, column ) )
	{
		if constexpr ( OptionalValue< T >::optional )
			return std::nullopt;
		else
			throw DBDecodeError(
				"NULL column requested as non-optional", static_cast< std::size_t >( column ), expected, actual );
	}
	const auto size { static_cast< std::size_t >( PQgetlength( result, row, column ) ) };
	const auto* data { reinterpret_cast< const std::byte* >( PQgetvalue( result, row, column ) ) };
	try
	{
		return DBCodec< T >::decode( { data, size } );
	}
	catch ( const DBDecodeError& error )
	{
		throw DBDecodeError( error.message, static_cast< std::size_t >( column ), expected, actual );
	}
}

template < typename... Ts, std::size_t... Is >
void validateColumns( PGresult* result, std::index_sequence< Is... > )
{
	( validateColumn< Ts >( result, Is ), ... );
}

template < typename... Ts, std::size_t... Is >
std::tuple< Ts... > decodeRow( PGresult* result, int row, std::index_sequence< Is... > )
{
	return { decodeColumn< Ts >( result, row, static_cast< int >( Is ) )... };
}
} // namespace detail

template < typename... Ts >
DBRows< Ts... >::DBRows( DBResult result, std::string sql ) : m_result( std::move( result ) ), m_sql( std::move( sql ) )
{
	try
	{
		if ( !m_result ) throw DBDecodeError( "Missing PostgreSQL result", 0, 0, 0 );
		const auto status { PQresultStatus( get() ) };
		if ( status != PGRES_TUPLES_OK && status != PGRES_TUPLES_CHUNK && status != PGRES_SINGLE_TUPLE )
			throw DBDecodeError( "Result does not contain rows", 0, 0, 0 );

		if ( PQnfields( get() ) == 0 || static_cast< std::size_t >( PQnfields( get() ) ) != sizeof...( Ts ) )
			throw DBDecodeError( "Column count mismatch or missing row description", 0, 0, 0 );
		detail::validateColumns< Ts... >( get(), std::index_sequence_for< Ts... > {} );
	}
	catch ( const DBDecodeError& error )
	{
		throw contextualError( error );
	}
}

template < typename... Ts >
DBDecodeError DBRows< Ts... >::contextualError( const DBDecodeError& error, std::optional< std::size_t > row ) const
{
	const char* name { m_result ? PQfname( get(), static_cast< int >( error.column ) ) : nullptr };
	return DBDecodeError {
		error.message, error.column, error.expected_oid, error.actual_oid, name ? name : "", m_sql, row
	};
}

template < typename... Ts >
DBRows< Ts... >::DBRows( DBRows&& ) noexcept = default;
template < typename... Ts >
DBRows< Ts... >& DBRows< Ts... >::operator=( DBRows&& ) noexcept = default;
template < typename... Ts >
DBRows< Ts... >::~DBRows() = default;

template < typename... Ts >
std::size_t DBRows< Ts... >::size() const noexcept
{
	return m_result ? static_cast< std::size_t >( PQntuples( get() ) ) : 0;
}

template < typename... Ts >
bool DBRows< Ts... >::empty() const noexcept
{
	return size() == 0;
}

template < typename... Ts >
std::tuple< Ts... > DBRows< Ts... >::row( std::size_t index ) const
{
	if ( index >= size() ) throw std::out_of_range( "PostgreSQL row index out of range" );
	try
	{
		return detail::decodeRow< Ts... >( get(), static_cast< int >( index ), std::index_sequence_for< Ts... > {} );
	}
	catch ( const DBDecodeError& error )
	{
		throw contextualError( error, index );
	}
}

template < typename... Ts >
PGresult* DBRows< Ts... >::get() const noexcept
{
	return m_result.get();
}

template < typename... Ts >
DBResult DBRows< Ts... >::release() &&
{
	return std::move( m_result );
}

template < typename... Ts >
DBRows< Ts... >::Iterator::Iterator( const DBRows* rows, std::size_t index ) : m_rows( rows ), m_index( index )
{}

template < typename... Ts >
DBRows< Ts... >::Iterator::Iterator() : m_rows( nullptr ), m_index( 0 )
{}

template < typename... Ts >
auto DBRows< Ts... >::Iterator::operator*() const -> value_type
{
	return m_rows->row( m_index );
}

template < typename... Ts >
auto DBRows< Ts... >::Iterator::operator++() -> Iterator&
{
	++m_index;
	return *this;
}

template < typename... Ts >
auto DBRows< Ts... >::Iterator::operator++( int ) -> Iterator
{
	auto previous { *this };
	++*this;
	return previous;
}

template < typename... Ts >
bool DBRows< Ts... >::Iterator::operator==( const Iterator& other ) const
{
	return m_rows == other.m_rows && m_index == other.m_index;
}

template < typename... Ts >
auto DBRows< Ts... >::begin() const -> Iterator
{
	return Iterator( this, 0 );
}

template < typename... Ts >
auto DBRows< Ts... >::end() const -> Iterator
{
	return Iterator( this, size() );
}
} // namespace idhan::db
