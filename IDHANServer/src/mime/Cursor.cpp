//
// Created by kj16609 on 10/21/25.
//

#include "Cursor.hpp"

#include "filesystem/IOUring.hpp"
#include "logging/log.hpp"
#include "spdlog/fmt/bin_to_hex.h"

namespace idhan::mime
{

drogon::Task< void > CursorData::requestData( const std::size_t offset, const std::size_t required_size ) const
{
	log::debug( "Asking for {} bytes at {}", required_size, offset );
	if ( std::holds_alternative< FileIOUring >( m_io ) )
	{
		auto& uring = std::get< FileIOUring >( m_io );
		m_buffer = co_await uring.read( offset, std::max( required_size, min_request_size ) );
		m_buffer_pos = offset;
		co_return;
	}
	if ( std::holds_alternative< std::string_view >( m_io ) )
	{
		// explicitly left as NOOP
		co_return;
	}

	throw std::runtime_error( "Unable to read data from file. No implemented reader for variant" );
}

drogon::Task< std::pair< const std::byte*, std::size_t > > CursorData::
	data( const std::size_t pos, const std::size_t required_size ) const
{
	if ( std::holds_alternative< FileIOUring >( m_io ) )
	{
		// buffer is for a range greater then the current pos. We need to go back
		const bool is_low { pos < m_buffer_pos };

		// buffer is not large enough. we need more data
		const bool is_small { required_size > m_buffer.size() };

		// access would not be in bounds
		const auto buffer_start { m_buffer_pos };
		const auto buffer_size { m_buffer.size() };
		const auto pos_start { pos };
		const auto pos_size { required_size };

		// check that buffer and pos overlap
		const auto is_oob { pos_start > buffer_start + buffer_size
			                || pos_start + pos_size > buffer_start + buffer_size };

		if ( is_low || is_small || is_oob )
		{
			log::debug(
				"access is lower than buffer: {}, access is larger than buffer: {}, access would oob: {}",
				is_low,
				is_small,
				is_oob );
			co_await requestData( pos, required_size );
		}

		FGL_ASSERT( m_buffer_pos <= pos, "Buffer was not expected at it's current pos" );
		const std::size_t offset { pos - m_buffer_pos };

		co_return std::make_pair( m_buffer.data() + offset, m_buffer.size() );
	}
	if ( std::holds_alternative< std::string_view >( m_io ) )
	{
		const auto& string_view { std::get< std::string_view >( m_io ) };
		const auto* data_ptr { reinterpret_cast< const std::byte* >( string_view.data() ) };
		const std::size_t length { string_view.size() };
		if ( pos + required_size >= length ) throw std::runtime_error( "OOB" );
		if ( length < pos ) throw std::runtime_error( "OOB" );
		m_buffer_pos = pos;
		co_return std::make_pair( data_ptr + pos, length - pos );
	}

	throw std::runtime_error( "Unable to read data from file. No implemented reader for variant" );
}

std::size_t CursorData::size() const
{
	if ( std::holds_alternative< FileIOUring >( m_io ) ) return std::get< FileIOUring >( m_io ).size();
	if ( std::holds_alternative< std::string_view >( m_io ) ) return std::get< std::string_view >( m_io ).size();
	throw std::runtime_error( "Unable to get size of data. No implemented reader for variant" );
}

Cursor::Cursor( FileIOUring uring ) : m_data( std::make_shared< CursorData >( uring ) )
{}

Cursor::Cursor( std::string_view view ) : m_data( std::make_shared< CursorData >( view ) )
{}

std::size_t Cursor::size() const
{
	return m_data->size();
}

coro::ImmedientTask< bool > Cursor::tryMatch( const std::string_view match ) const
{
	FGL_ASSERT( m_data, "Data was invalid" );
	const auto [ ptr, size ] { co_await m_data->data( m_pos, match.size() ) };

	if ( !ptr ) co_return false;

	if ( size < match.size() ) co_return false;

	// if memcmp returns zero, They are identical.
	const bool passes { std::memcmp( ptr, match.data(), match.size() ) == 0 };

	co_return passes;
}

coro::ImmedientTask< bool > Cursor::tryMatchInc( const std::string_view match )
{
	const bool is_match { co_await tryMatch( match ) };
	if ( is_match ) inc( match.size() );
	co_return is_match;
}

void Cursor::jumpTo( const std::int64_t pos )
{
	if ( pos < 0 ) m_pos = size() - static_cast< std::size_t >( std::abs( pos ) );
	m_pos = static_cast< std::size_t >( pos );
}

bool Cursor::inc( const std::size_t i )
{
	m_pos += i;
	return m_pos < size();
}

void Cursor::dec( const std::size_t i )
{
	m_pos -= i;
}

std::size_t Cursor::pos() const
{
	return m_pos;
}

} // namespace idhan::mime