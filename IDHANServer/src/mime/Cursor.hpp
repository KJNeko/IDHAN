#pragma once
#include <json/value.h>

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

#include "drogon/utils/coroutine.h"
#include "filesystem/io/IOUring.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::mime
{

constexpr std::size_t min_request_size { 32 * 1024 };

class CursorData
{
	std::variant< std::shared_ptr< FileIOUring >, std::string_view > m_io;

	mutable std::size_t m_buffer_pos { 0 };
	mutable std::vector< std::byte > m_buffer {};

	IDHANTask<> requestData( std::size_t offset, std::size_t required_size ) const;

	IDHANTask< std::pair< const std::byte*, size_t > > checkData( std::size_t pos, std::size_t required_size ) const;

	friend class Cursor;

	std::size_t size() const;

  public:

	FGL_DELETE_ALL_RO5( CursorData );

	CursorData( std::shared_ptr< FileIOUring > uring ) : m_io { std::move( uring ) } {}

	CursorData( std::string_view data ) noexcept : m_io { data } {}
};

class Cursor
{
	std::shared_ptr< FileIOUring > m_io;
	std::shared_ptr< CursorData > m_data {};
	std::size_t m_pos { 0 };
	std::string m_extension { "" };

	using Priority = int;

  public:

	Cursor() = delete;
	Cursor( std::shared_ptr< FileIOUring > uring );
	Cursor( std::string_view view, const std::string& file_name );

	FGL_DEFAULT_COPY( Cursor );
	FGL_DEFAULT_MOVE( Cursor );

	std::size_t size() const;
	drogon::Task< std::string_view > data( std::size_t size ) const;

	[[nodiscard]] drogon::Task< bool > tryMatch( std::string_view match ) const;

	[[nodiscard]] drogon::Task< bool > tryMatchInc( std::string_view match );

	[[nodiscard]] std::string_view fileExtension() const { return m_extension; }

	void jumpTo( std::int64_t pos );

	[[nodiscard]] bool inc( std::size_t i = 1 );

	void dec( std::size_t i = 1 );

	[[nodiscard]] std::size_t pos() const;
};

} // namespace idhan::mime
