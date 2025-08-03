//
// Created by kj16609 on 8/2/25.
//
#pragma once

#include "pgtypes_numeric.h"

namespace drogon::orm::internal
{

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

// Helper function to create PostgreSQL binary array format
template < typename T >
std::vector< char > createPgBinaryArray( const std::vector< T >& data )
{
	static_assert( std::is_trivially_copyable_v< T >, "Type must be trivially copyable for binary arrays" );

	// PostgreSQL binary array format:
	// - 4 bytes: number of dimensions (int32, network byte order)
	// - 4 bytes: flags (int32, network byte order) - 0 for no nulls
	// - 4 bytes: element type OID (int32, network byte order)
	// - For each dimension:
	//   - 4 bytes: dimension length (int32, network byte order)
	//   - 4 bytes: lower bound (int32, network byte order) - usually 1
	// - For each element:
	//   - 4 bytes: element length (int32, network byte order)
	//   - N bytes: element data

	struct Header
	{
		uint32_t num_dimensions;
		uint32_t flags;
		uint32_t element_type_oid;
		uint32_t dimension_length;
		uint32_t lower_bound;
	};

	struct Element
	{
		uint32_t element_length;
		T data {};
	};

	static_assert( sizeof( Header ) == sizeof( int32_t ) * 5, "Header is not sized properly" );

	std::vector< char > result {};
	result.resize( sizeof( Header ) + ( sizeof( Element ) * data.size() ) );

	Header* header = reinterpret_cast< Header* >( result.data() );
	header->num_dimensions = 1;
	header->flags = 0;
	header->element_type_oid = getTypeOid< T >();
	header->dimension_length = static_cast< uint32_t >( data.size() );
	header->lower_bound = 1;

	Element* elements = reinterpret_cast< Element* >( result.data() + sizeof( Header ) );
	for ( std::size_t i = 0; i < data.size(); ++i )
	{
		auto& element { elements[ i ] };
		element.element_length = static_cast< int32_t >( sizeof( T ) );
		if constexpr ( sizeof( T ) == 2 )
			element.data = htons( data[ i ] );
		else if constexpr ( sizeof( T ) == 4 )
			element.data = htonl( data[ i ] );
		else if constexpr ( sizeof( T ) == 8 )
			element.data = htonll( data[ i ] );
		element.element_length = htonl( element.element_length );
	}

	// convert BinaryData to network byte order
	header->num_dimensions = htonl( header->num_dimensions );
	header->flags = htonl( header->flags );
	header->element_type_oid = htonl( header->element_type_oid );
	header->dimension_length = htonl( header->dimension_length );
	header->lower_bound = htonl( header->lower_bound );

	return result;
}

// Generic template for integer types
template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::SmallInt > >( std::vector< idhan::SmallInt >&&
                                                                                      param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< char > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( binary_data->data() );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::Integer > >( std::vector< idhan::Integer >&& param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< char > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( binary_data->data() );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::BigInt > >( std::vector< idhan::BigInt >&& param )
{
	++parametersNumber_;

	auto binary_data = std::make_shared< std::vector< char > >( createPgBinaryArray( param ) );
	objs_.push_back( binary_data );

	parameters_.push_back( binary_data->data() );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

} // namespace drogon::orm::internal