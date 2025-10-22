//
// Created by kj16609 on 10/21/25.
//
#pragma once
#include <json/value.h>

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

#include "drogon/utils/coroutine.h"
#include "filesystem/IOUring.hpp"

namespace idhan::mime
{

constexpr std::size_t min_request_size { 1024 };

class CursorData
{
	std::variant< FileIOUring, std::string_view > m_io;

	mutable std::size_t m_buffer_pos { 0 };
	mutable std::vector< std::byte > m_buffer {};

	//! Populates the buffer with data from the offset and at least required_size
	drogon::Task< void > requestData( std::size_t offset, std::size_t required_size ) const;

	drogon::Task< std::pair< const std::byte*, std::size_t > > data( std::size_t pos, std::size_t required_size ) const;

	friend class Cursor;

	std::size_t size() const
	{
		if ( std::holds_alternative< FileIOUring >( m_io ) ) return std::get< FileIOUring >( m_io ).size();
		if ( std::holds_alternative< std::string_view >( m_io ) ) return std::get< std::string_view >( m_io ).size();
		throw std::runtime_error( "Unable to get size of data. No implemented reader for variant" );
	}

  public:

	CursorData() = delete;

	CursorData( FileIOUring uring ) : m_io { uring } {}

	CursorData( std::string_view data ) : m_io { data } {}
};

class Cursor
{
	std::shared_ptr< CursorData > m_data {};
	std::size_t m_pos { 0 };

	using Priority = int;

	std::vector< std::pair< Priority, std::string > > m_matches {};

  public:

	Cursor() = delete;
	Cursor( FileIOUring uring );
	Cursor( std::string_view view );

	std::size_t size() const;

	//! Tries to match `match` with current cursor position.
	drogon::Task< bool > tryMatch( std::string_view match ) const;

	//! Tries to match `match` with the current cursor position, if matched then the cursor will jump forward by match.size()
	drogon::Task< bool > tryMatchInc( std::string_view match );

	void jumpTo( std::int64_t pos );

	bool inc( std::size_t i = 1 );

	void dec( std::size_t i = 1 );

	std::size_t pos() const;
};

} // namespace idhan::mime