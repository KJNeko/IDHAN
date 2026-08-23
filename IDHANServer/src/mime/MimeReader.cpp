#include "MimeReader.hpp"

#include <algorithm>
#include <cstring>

#include "filesystem/io/IOUring.hpp"

namespace idhan::mime
{

MimeReader::MimeReader( std::shared_ptr< FileIOUring > file ) : m_source( std::move( file ) )
{}

MimeReader::MimeReader( const std::span< const std::byte > data ) : m_source( data )
{}

MimeReader::MimeReader( const std::span< const std::uint8_t > data ) :
  m_source( std::span< const std::byte > { reinterpret_cast< const std::byte* >( data.data() ), data.size() } )
{}

MimeReader::MimeReader( const std::string_view data ) :
  m_source( std::span< const std::byte > { reinterpret_cast< const std::byte* >( data.data() ), data.size() } )
{}

std::size_t MimeReader::size() const
{
	if ( const auto* data { std::get_if< std::span< const std::byte > >( &m_source ) } ) return data->size();

	return std::get< std::shared_ptr< FileIOUring > >( m_source )->size();
}

IDHANTask< std::span< const std::byte > > MimeReader::at( const std::size_t offset, const std::size_t length ) const
{
	const auto total { size() };

	if ( offset >= total ) co_return {};

	const std::size_t wanted { std::min( length, total - offset ) };

	if ( const auto* data { std::get_if< std::span< const std::byte > >( &m_source ) } )
		co_return data->subspan( offset, wanted );

	const auto& file { std::get< std::shared_ptr< FileIOUring > >( m_source ) };

	const bool covered { offset >= m_window_offset && offset + wanted <= m_window_offset + m_window.size() };

	if ( !covered )
	{
		m_window = co_await file->read( offset, std::max( wanted, min_read_size ) );
		m_window_offset = offset;
	}

	const std::size_t start { offset - m_window_offset };

	// a short read leaves the window holding less than was asked for
	const std::size_t available { std::min( wanted, m_window.size() - start ) };

	co_return std::span< const std::byte > { m_window.data() + start, available };
}

IDHANTask< bool > MimeReader::matchAt( const std::size_t offset, const std::string_view pattern ) const
{
	if ( pattern.empty() ) co_return false;

	const auto data { co_await at( offset, pattern.size() ) };

	if ( data.size() < pattern.size() ) co_return false;

	co_return std::memcmp( data.data(), pattern.data(), pattern.size() ) == 0;
}

IDHANTask< std::optional< std::size_t > > MimeReader::find(
	const std::string_view pattern,
	const std::size_t from,
	const std::size_t limit ) const
{
	if ( pattern.empty() || limit == 0 ) co_return std::nullopt;

	const auto total { size() };

	if ( from >= total ) co_return std::nullopt;

	// the last offset a match may start at, exclusive
	const std::size_t last { std::min( from + limit, total ) };

	for ( std::size_t pos { from }; pos < last; )
	{
		// a match starting just below `last` still runs pattern.size() bytes past it
		const std::size_t span_len { ( last - pos ) + pattern.size() - 1 };
		const auto chunk { co_await at( pos, std::clamp( span_len, pattern.size(), min_read_size ) ) };

		if ( chunk.size() < pattern.size() ) co_return std::nullopt;

		const std::string_view haystack { reinterpret_cast< const char* >( chunk.data() ), chunk.size() };

		if ( const auto found { haystack.substr( 0, std::min( haystack.size(), span_len ) ).find( pattern ) };
		     found != std::string_view::npos )
			co_return pos + found;

		// overlap by pattern.size() - 1 so a match straddling the chunk boundary is not missed
		pos += haystack.size() - ( pattern.size() - 1 );
	}

	co_return std::nullopt;
}

} // namespace idhan::mime
