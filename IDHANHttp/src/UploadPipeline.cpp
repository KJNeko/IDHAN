#include "idhan/http/UploadPipeline.hpp"

#include <folly/FileUtil.h>

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <limits>
#include <span>
#include <system_error>
#include <unistd.h>

namespace idhan::http
{
static folly::File createUploadFile( const std::filesystem::path& directory )
{
	std::filesystem::create_directories( directory );
	const int unnamed { ::open( directory.c_str(), O_TMPFILE | O_RDWR | O_CLOEXEC, 0600 ) };
	if ( unnamed >= 0 ) return folly::File { unnamed, true };

	if ( errno != EOPNOTSUPP && errno != EINVAL && errno != EISDIR )
		throw std::system_error( errno, std::generic_category(), "Creating temporary upload" );

	auto name { ( directory / "upload-XXXXXX" ).string() };
	const int descriptor { ::mkostemp( name.data(), O_CLOEXEC ) };
	if ( descriptor < 0 ) throw std::system_error( errno, std::generic_category(), "Creating temporary upload" );

	folly::File file { descriptor, true };
	if ( ::unlink( name.c_str() ) != 0 )
		throw std::system_error( errno, std::generic_category(), "Unlinking temporary upload" );

	return file;
}

UploadPipeline::UploadPipeline( std::filesystem::path temp_path ) : m_temp_path( std::move( temp_path ) )
{}

void UploadPipeline::write( const folly::IOBuf& chunk )
{
	for ( const auto bytes : chunk )
	{
		if ( bytes.size() > static_cast< std::uint64_t >( std::numeric_limits< off_t >::max() ) - m_size )
			throw std::overflow_error( "Temporary upload exceeds file offset range" );

		const auto written {
			folly::pwriteFull( m_file.fd(), bytes.data(), bytes.size(), static_cast< off_t >( m_size ) )
		};
		if ( written < 0 ) throw std::system_error( errno, std::generic_category(), "Writing temporary upload" );

		if ( static_cast< std::size_t >( written ) != bytes.size() )
			throw std::runtime_error( "Short temporary upload write" );

		m_digest.update(
			std::span< const std::byte > { reinterpret_cast< const std::byte* >( bytes.data() ), bytes.size() } );
		m_size += bytes.size();
	}
}

runtime::Task<> UploadPipeline::append( std::unique_ptr< folly::IOBuf > chunk )
{
	if ( m_finished ) throw std::logic_error( "Upload already completed" );

	if ( !chunk ) co_return;

	if ( !m_file && chunk->computeChainDataLength() <= request_memory_limit - m_buffer.chainLength() )
	{
		m_buffer.append( std::move( chunk ) );
		co_return;
	}

	if ( !m_file )
	{
		m_file = createUploadFile( m_temp_path );
		auto buffered { m_buffer.move() };
		if ( buffered ) write( *buffered );
	}
	write( *chunk );
}

runtime::Task< RequestBody > UploadPipeline::finish()
{
	if ( m_finished ) throw std::logic_error( "Upload already completed" );

	m_finished = true;
	if ( !m_file )
	{
		auto buffered { m_buffer.move() };
		if ( !buffered ) buffered = folly::IOBuf::create( 0 );

		co_return RequestBody { std::move( buffered ) };
	}

	// This file contains the complete raw body; an original filename is not inferred from headers.
	co_return RequestBody { TemporaryUpload { std::move( m_file ), m_size, m_digest.finish(), {} } };
}
} // namespace idhan::http
