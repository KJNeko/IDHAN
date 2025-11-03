//
// Created by kj16609 on 8/2/25.
//
#pragma once

#include "crypto/SHA256.hpp"

std::vector< std::byte > createPgBinaryArray( std::vector< idhan::SHA256 >&& data );
std::vector< std::byte > createPgBinaryArray( std::vector< std::string >&& strings );
std::vector< std::byte > createPgBinaryArray( std::set< std::string >&& strings );
std::vector< std::byte > createPgBinaryArray( std::vector< idhan::SmallInt >&& data );
std::vector< std::byte > createPgBinaryArray( std::vector< idhan::Int >&& data );
std::vector< std::byte > createPgBinaryArray( std::vector< idhan::BigInt >&& data );

std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::SHA256 >&& data );
std::vector< std::byte > createPgBinaryArray( const std::vector< std::string >&& strings );
std::vector< std::byte > createPgBinaryArray( const std::set< std::string >&& strings );
std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::SmallInt >&& data );
std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::Int >&& data );
std::vector< std::byte > createPgBinaryArray( const std::vector< idhan::BigInt >&& data );

namespace drogon::orm::internal
{

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::SHA256 > >( std::vector< idhan::SHA256 >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< std::vector< idhan::SHA256 > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < const std::vector< idhan::SHA256 > >(
	const std::vector< idhan::SHA256 >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< const std::vector< idhan::SHA256 > >( param ) ) ) };
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

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< std::vector< std::string > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 );

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < const std::vector< std::string > >(
	const std::vector< std::string >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< const std::vector< std::string > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 );

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::set< std::string > >( std::set< std::string >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< std::set< std::string > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 );

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < const std::set< std::string > >( const std::set< std::string >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< const std::set< std::string > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 );

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < std::vector< idhan::SmallInt > >(
	std::vector< idhan::SmallInt >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< std::vector< idhan::SmallInt > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < const std::vector< idhan::SmallInt > >(
	const std::vector< idhan::SmallInt >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< const std::vector< idhan::SmallInt > >( param ) ) ) };
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

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< std::vector< idhan::Int > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < const std::vector< idhan::Int > >(
	const std::vector< idhan::Int >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< const std::vector< idhan::Int > >( param ) ) ) };
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

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< std::vector< idhan::BigInt > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}

template <>
inline SqlBinder::self& SqlBinder::operator<< < const std::vector< idhan::BigInt > >(
	const std::vector< idhan::BigInt >&& param )
{
	++parametersNumber_;

	const auto binary_data { std::make_shared< std::vector< std::byte > >(
		createPgBinaryArray( std::forward< const std::vector< idhan::BigInt > >( param ) ) ) };
	objs_.push_back( binary_data );

	parameters_.push_back( reinterpret_cast< const char* >( binary_data->data() ) );
	lengths_.push_back( static_cast< int >( binary_data->size() ) );
	formats_.push_back( 1 ); // Binary format

	return *this;
}
} // namespace drogon::orm::internal
