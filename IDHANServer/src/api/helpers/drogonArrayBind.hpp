//
// Created by kj16609 on 8/2/25.
//
#pragma once

#include <codecvt>
#include <locale>

#include "crypto/SHA256.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "pgEscape.hpp"

namespace drogon::orm::internal
{

constexpr auto OID_BYTEA { 17 };
constexpr auto OID_TEXT { 25 };

// Get PostgreSQL OID for the type
template < typename T >
constexpr int32_t getTypeOid()
{
	if constexpr ( std::is_same_v< T, int32_t > )
		return 23; // INT4OID
	else if constexpr ( std::is_same_v< T, int64_t > )
		return 20; // INT8OID
	else if constexpr ( std::is_same_v< T, int16_t > )
		return 21; // INT2OID
	else if constexpr ( std::is_same_v< T, float > )
		return 700; // FLOAT4OID
	else if constexpr ( std::is_same_v< T, double > )
		return 701; // FLOAT8OID
	else
		return 23; // Default to INT4 for other integer types
}

struct Header
{
	uint32_t num_dimensions;
	uint32_t flags;
	uint32_t element_type_oid;
	uint32_t dimension_length;
	uint32_t lower_bound;
};

static_assert( sizeof( Header ) == sizeof( std::uint32_t ) * 5, "Header is not sized properly" );

// Helper function to create PostgreSQL binary array format
template < typename T >
std::vector< std::byte > createPgBinaryArray( const std::vector< T >& data )
{
	static_assert( std::is_trivially_copyable_v< T >, "Type must be trivially copyable for binary arrays" );

	struct Element
	{
		uint32_t element_length { 0 };
		T data {};
	};

	std::vector< std::byte > result {};
	result.resize( sizeof( Header ) + ( sizeof( Element ) * data.size() ) );

	auto header { reinterpret_cast< Header* >( result.data() ) };
	header->num_dimensions = htonl( 1 );
	header->flags = htonl( 0 );
	header->element_type_oid = htonl( getTypeOid< T >() );
	header->dimension_length = htonl( static_cast< uint32_t >( data.size() ) );
	header->lower_bound = htonl( 1 );

	Element* elements = reinterpret_cast< Element* >( result.data() + sizeof( Header ) );
	for ( std::size_t i = 0; i < data.size(); ++i )
	{
		auto& element { elements[ i ] };
		element.element_length = htonl( static_cast< int32_t >( sizeof( T ) ) );
		if constexpr ( sizeof( T ) == 2 )
			element.data = htons( data[ i ] );
		else if constexpr ( sizeof( T ) == 4 )
			element.data = htonl( data[ i ] );
		else if constexpr ( sizeof( T ) == 8 )
			element.data = htonll( data[ i ] );
	}

	return result;
}

template <>
inline std::vector< std::byte > createPgBinaryArray< std::string >( const std::vector< std::string >& strings )
{
	std::vector< std::byte > result {};

	std::size_t string_sizes { 0 };
	for ( const auto& str : strings ) string_sizes += str.size();

	struct Element
	{
		std::uint32_t element_length { 0 };
	};

	static_assert( sizeof( Element ) == sizeof( std::uint32_t ), "Element is not sized properly" );

	result.resize( ( sizeof( Header ) + ( sizeof( Element ) * strings.size() ) + string_sizes ) * 1 );
	std::memset( result.data(), 0, result.size() );

	Header* header = reinterpret_cast< Header* >( result.data() );
	header->num_dimensions = htonl( 1 ); // dimension count
	header->flags = htonl( 0 ); // any nulls?
	header->element_type_oid = htonl( OID_TEXT ); // element type
	header->dimension_length = htonl( static_cast< uint32_t >( strings.size() ) ); // size of first dimension
	header->lower_bound = htonl( 1 ); // offset of first dimension

	std::byte* ptr = result.data() + sizeof( Header );
	for ( const auto& str : strings )
	{
		auto& element = *reinterpret_cast< Element* >( ptr );
		// const auto filtered_string { idhan::api::helpers::pgEscape( str ) };
		const auto filtered_string { str };
		element.element_length = htonl( filtered_string.size() );
		ptr += sizeof( Element );
		std::memcpy( ptr, filtered_string.data(), filtered_string.size() );
		ptr += filtered_string.size();
	}

	return result;
}

template <>
inline std::vector< std::byte > createPgBinaryArray< idhan::SHA256 >( const std::vector< idhan::SHA256 >& data )
{
	std::vector< std::byte > result {};

	struct Element
	{
		std::uint32_t element_length { idhan::SHA256::size() };
		std::array< std::byte, idhan::SHA256::size() > data {};
	};

	result.resize( sizeof( Header ) + ( sizeof( Element ) * data.size() ) );

	Header* header = reinterpret_cast< Header* >( result.data() );
	header->num_dimensions = 1; // dimension count
	header->flags = 0; // any nulls?
	header->element_type_oid = OID_BYTEA; // element type
	header->dimension_length = static_cast< uint32_t >( data.size() ); // size of first dimension
	header->lower_bound = 1; // offset of first dimension

	Element* elements = reinterpret_cast< Element* >( result.data() + sizeof( Header ) );
	for ( std::size_t i = 0; i < data.size(); ++i )
	{
		auto& element { elements[ i ] };
		std::memcpy( element.data.data(), data[ i ].data().data(), idhan::SHA256::size() );
	}

	return result;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::SHA256 > >( std::vector< idhan::SHA256 >&& param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< std::byte > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< std::string > >( std::vector< std::string >&& param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< std::byte > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 );

	return *this;
}

// Generic template for integer types
template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::SmallInt > >( std::vector< idhan::SmallInt >&&
                                                                                      param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< std::byte > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::Integer > >( std::vector< idhan::Integer >&& param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< std::byte > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::BigInt > >( std::vector< idhan::BigInt >&& param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< std::byte > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

} // namespace drogon::orm::internal