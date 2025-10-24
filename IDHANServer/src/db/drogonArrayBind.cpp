//
// Created by kj16609 on 8/6/25.
//

#include "drogonArrayBind.hpp"

#include "../../../IDHANClient/include/idhan/logging/logger.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "spdlog/fmt/bin_to_hex.h"

constexpr auto OID_BYTEA { 17 };
constexpr auto OID_TEXT { 25 };

// Get PostgreSQL OID for the type
template < typename T >
consteval int32_t getTypeOid()
{
	if constexpr ( std::is_same_v< T, idhan::SmallInt > )
		return 21; // INT2OID
	else if constexpr ( std::is_same_v< T, idhan::Int > )
		return 23; // INT4OID
	else if constexpr ( std::is_same_v< T, idhan::BigInt > )
		return 20; // INT8OID
	else if constexpr ( std::is_same_v< T, float > )
		return 700; // FLOAT4OID
	else if constexpr ( std::is_same_v< T, double > )
		return 701; // FLOAT8OID
	else
		static_assert( false, "No OID value for type T" );
	FGL_UNREACHABLE();
}

struct Header
{
	std::uint32_t num_dimensions; // <ndim>
	std::uint32_t data_offset; // <dataoffset>
	std::uint32_t element_type_oid; // <elemtype>
	std::uint32_t dimension_length; // <dimension>
	std::uint32_t lower_bound; // <lower bnds>

	// optional <null bitmap>
	// <data>
};

static_assert( sizeof( Header ) == sizeof( std::uint32_t ) * 5, "Header is not sized properly" );

template < typename T >
std::vector< std::byte > createPgBinaryArrayScalar( const std::vector< T >& data )
{
	struct [[gnu::packed]] Element
	{
		uint32_t element_length { 0 };
		T data {};
	};

	static_assert( sizeof( Element ) == sizeof( std::uint32_t ) + sizeof( T ), "Element struct is not packed" );

	std::vector< std::byte > result {};
	result.resize( sizeof( Header ) + ( sizeof( Element ) * data.size() ) );

	auto header { reinterpret_cast< Header* >( result.data() ) };
	header->num_dimensions = htonl( 1 );
	header->data_offset = htonl( 0 );
	header->element_type_oid = htonl( getTypeOid< T >() );
	header->dimension_length = htonl( static_cast< uint32_t >( data.size() ) );
	header->lower_bound = htonl( 1 );

	auto* elements = reinterpret_cast< Element* >( result.data() + sizeof( Header ) );
	for ( std::size_t i = 0; i < data.size(); ++i )
	{
		auto& element { elements[ i ] };
		element.element_length = htonl( sizeof( T ) );

		static_assert(
			std::same_as< T, std::int64_t > || std::same_as< T, std::int32_t > || std::same_as< T, std::int16_t >,
			"Invalid type" );

		static_assert( sizeof( element.data ) == sizeof( data[ i ] ), "Size mismatch" );

		if constexpr ( std::same_as< T, std::int64_t > )
			element.data = htonll( data[ i ] );
		else if constexpr ( std::same_as< T, std::int32_t > )
			element.data = htonl( data[ i ] );
		else if constexpr ( std::same_as< T, std::int16_t > )
			element.data = htons( data[ i ] );
	}

	return result;
}

std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::SmallInt >& data )
{
	return createPgBinaryArrayScalar< idhan::SmallInt >( data );
}

std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::Int >& data )
{
	return createPgBinaryArrayScalar< idhan::Int >( data );
}

std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::BigInt >& data )
{
	return createPgBinaryArrayScalar< idhan::BigInt >( data );
}

std::vector< std::byte > createPgBinaryArray( const std::vector< std::string >& strings )
{
	std::vector< std::byte > result {};

	std::size_t string_sizes { 0 };
	for ( const auto& str : strings ) string_sizes += str.size();

	struct [[gnu::packed]] Element
	{
		std::uint32_t element_length { 0 };
	};

	static_assert( sizeof( Element ) == sizeof( std::uint32_t ), "Element is not sized properly" );

	result.resize( sizeof( Header ) + ( sizeof( Element ) * strings.size() ) + string_sizes );
	std::memset( result.data(), 0xFF, result.size() );

	auto* header = reinterpret_cast< Header* >( result.data() );
	header->num_dimensions = htonl( 1 ); // dimension count
	header->data_offset = htonl( 0 ); // any nulls?
	header->element_type_oid = htonl( OID_TEXT ); // element type
	header->dimension_length = htonl( static_cast< uint32_t >( strings.size() ) ); // size of first dimension
	header->lower_bound = htonl( 1 ); // offset of first dimension

	std::byte* ptr = result.data() + sizeof( Header );
	for ( const auto& str : strings )
	{
		auto& element = *reinterpret_cast< Element* >( ptr );
		// const auto filtered_string { idhan::api::helpers::pgEscape( str ) };
		element.element_length = htonl( static_cast< std::uint32_t >( str.size() ) );
		std::memcpy( ptr + sizeof( Element ), str.data(), str.size() );
		ptr += sizeof( Element ) + str.size();
	}

	return result;
}

std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::SHA256 >& data )
{
	std::vector< std::byte > result {};

	struct Element
	{
		std::uint32_t element_length { 0 };
		std::array< std::byte, idhan::SHA256::size() > data {};
	};

	result.resize( sizeof( Header ) + ( sizeof( Element ) * data.size() ) );

	auto* header = reinterpret_cast< Header* >( result.data() );
	header->num_dimensions = htonl( 1 ); // dimension count
	header->data_offset = htonl( 0 ); // any nulls?
	header->element_type_oid = htonl( OID_BYTEA ); // element type
	header->dimension_length = htonl( static_cast< uint32_t >( data.size() ) ); // size of first dimension
	header->lower_bound = htonl( 1 ); // offset of first dimension

	auto* elements = reinterpret_cast< Element* >( result.data() + sizeof( Header ) );
	for ( std::size_t i = 0; i < data.size(); ++i )
	{
		auto& element { elements[ i ] };
		element.element_length = htonl( idhan::SHA256::size() );
		std::memcpy( element.data.data(), data[ i ].data().data(), idhan::SHA256::size() );
	}

	return result;
}