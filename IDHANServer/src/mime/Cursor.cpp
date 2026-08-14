#include "Cursor.hpp"

#include <tuple>

#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"

namespace idhan::mime
{

IDHANTask<> CursorData::requestData( const std::size_t offset, const std::size_t required_size ) const
{
	log::trace( "Requesting data at offset {} with size {}", offset, required_size );
	if ( std::holds_alternative< std::shared_ptr< FileIOUring > >( m_io ) )
	{
		const auto& uring = std::get< std::shared_ptr< FileIOUring > >( m_io );
		m_buffer = co_await uring->read( offset, std::max( required_size, min_request_size ) );
		m_buffer_pos = offset;
		co_return;
	}
	if ( std::holds_alternative< std::string_view >( m_io ) )
	{
		log::trace( "CursorData::requestData: string_view source, no read needed" );
		co_return;
	}

	throw std::runtime_error( "Unable to read data from file. No implemented reader for variant" );
}

IDHANTask< std::pair< const std::byte*, size_t > > CursorData::checkData(
	const std::size_t pos,
	const std::size_t required_size ) const
{
	if ( std::holds_alternative< std::shared_ptr< FileIOUring > >( m_io ) )
	{
		// buffer is for a range greater then the current pos. We need to go back
		const bool is_low { pos < m_buffer_pos };

		// buffer is not large enough. we need more data
		const bool is_small { required_size > m_buffer.size() };

		// access would not be in bounds: the requested range must end within the buffer
		const auto is_oob { pos + required_size > m_buffer_pos + m_buffer.size() };

		if ( is_low || is_small || is_oob )
		{
			log::trace(
				"CursorData::checkData: re-reading at pos {} (low={}, small={}, oob={})",
				pos,
				is_low,
				is_small,
				is_oob );
			co_await requestData( pos, required_size );
		}
		else
		{
			log::trace( "CursorData::checkData: buffer OK at pos {} (need {} bytes)", pos, required_size );
		}

		FGL_ASSERT( m_buffer_pos <= pos, "Buffer was not expected at it's current pos" );
		const std::size_t offset { pos - m_buffer_pos };

		const std::size_t available { offset <= m_buffer.size() ? m_buffer.size() - offset : 0 };

		co_return std::make_pair( m_buffer.data() + offset, available );
	}
	if ( std::holds_alternative< std::string_view >( m_io ) )
	{
		const auto& string_view { std::get< std::string_view >( m_io ) };
		const auto* data_ptr { reinterpret_cast< const std::byte* >( string_view.data() ) };
		const std::size_t length { string_view.size() };
		// pos + required_size == length is a valid exact fit ending on the last byte
		if ( pos + required_size > length || length < pos ) co_return std::make_pair( nullptr, 0 );
		const auto leftover_size { length - pos };
		m_buffer_pos = pos;
		co_return std::make_pair( data_ptr + pos, std::min( required_size, leftover_size ) );
	}

	throw std::runtime_error( "Unable to read data from file. No implemented reader for variant" );
}

std::size_t CursorData::size() const
{
	if ( std::holds_alternative< std::shared_ptr< FileIOUring > >( m_io ) )
		return std::get< std::shared_ptr< FileIOUring > >( m_io )->size();
	if ( std::holds_alternative< std::string_view >( m_io ) ) return std::get< std::string_view >( m_io ).size();
	throw std::runtime_error( "Unable to get size of data. No implemented reader for variant" );
}

Cursor::Cursor( std::shared_ptr< FileIOUring > uring ) :
  m_io( uring ),
  m_data( std::make_shared< CursorData >( uring ) ),
  m_extension( uring->path().extension().string() )
{}

Cursor::Cursor( std::string_view view, const std::string& file_name ) :
  m_data( std::make_shared< CursorData >( view ) ),
  m_extension( std::filesystem::path( file_name ).extension().string() )
{}

std::size_t Cursor::size() const
{
	return m_data->size();
}

drogon::Task< std::string_view > Cursor::data( const std::size_t d_size ) const
{
	const auto data_result { co_await m_data->checkData( m_pos, d_size ) };
	const auto [ ptr, size ] = data_result;

	co_return std::string_view { reinterpret_cast< const char* >( ptr ), size };
}

drogon::Task< bool > Cursor::tryMatch( const std::string_view match ) const
{
	FGL_ASSERT( m_data, "Data was invalid" );
	const auto [ ptr, size ] { co_await m_data->checkData( m_pos, match.size() ) };

	if ( !ptr )
	{
		log::trace( "Cursor::tryMatch: ptr was null at pos {} for {} bytes", m_pos, match.size() );
		co_return false;
	}

	if ( size < match.size() )
	{
		log::trace( "Cursor::tryMatch: not enough data at pos {}: need {} have {}", m_pos, match.size(), size );
		co_return false;
	}

	const bool passes { std::memcmp( ptr, match.data(), match.size() ) == 0 };
	log::trace( "Cursor::tryMatch: at pos {} -> {}", m_pos, passes ? "PASS" : "FAIL" );

	co_return passes;
}

drogon::Task< bool > Cursor::tryMatchInc( const std::string_view match )
{
	const bool is_match { co_await tryMatch( match ) };
	if ( is_match )
	{
		log::trace( "Cursor::tryMatchInc: advance by {} to pos {}", match.size(), m_pos + match.size() );
		std::ignore = inc( match.size() );
	}
	co_return is_match;
}

void Cursor::jumpTo( const std::int64_t pos )
{
	if ( pos < 0 )
	{
		const auto from_end { static_cast< std::size_t >( -pos ) };
		m_pos = from_end > size() ? size() : size() - from_end;
	}
	else
	{
		m_pos = static_cast< std::size_t >( pos );
	}
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
