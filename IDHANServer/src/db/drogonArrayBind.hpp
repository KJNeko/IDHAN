//
// Created by kj16609 on 8/2/25.
//
#pragma once

#include "crypto/SHA256.hpp"

std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::SHA256 >& data );
std::vector< std::byte > createPgBinaryArray( const std::vector< std::string >& data );
std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::SmallInt >& data );
std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::Int >& data );
std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::BigInt >& data );

namespace drogon::orm::internal
{

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
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::Int > >( std::vector< idhan::Int >&& param )
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