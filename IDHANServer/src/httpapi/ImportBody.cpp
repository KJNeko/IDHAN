#include "ImportBody.hpp"

#include <sys/mman.h>
#include <sys/stat.h>

#include <cerrno>
#include <limits>
#include <system_error>

namespace idhan::httpapi
{

void ImportBody::Unmap::operator()( const std::byte* bytes ) const noexcept
{
	::munmap( const_cast< std::byte* >( bytes ), size );
}

std::expected< ImportBody, std::string > ImportBody::create( http::RequestBody body )
{
	ImportBody result {};
	if ( std::holds_alternative< std::unique_ptr< folly::IOBuf > >( body ) )
	{
		result.m_buffer = std::move( std::get< std::unique_ptr< folly::IOBuf > >( body ) );
		result.m_buffer->coalesce();
		const auto data { result.bytes() };
		result.m_hash = SHA256::hash( data );
		return result;
	}

	auto& upload { std::get< http::TemporaryUpload >( body ) };
	result.m_hash = upload.hash();
	const auto size { upload.size() };
	if ( size > std::numeric_limits< std::size_t >::max() )
		return std::unexpected( "Upload exceeds the addressable file size" );

	auto file { std::move( upload ).releaseFile() };
	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 )
		return std::unexpected( std::system_error( errno, std::generic_category(), "Inspecting upload" ).what() );

	if ( !S_ISREG( status.st_mode ) || status.st_size < 0 || static_cast< std::uint64_t >( status.st_size ) != size )
		return std::unexpected( "Upload file size does not match received bytes" );

	if ( size == 0 ) return result;

	void* mapping { ::mmap( nullptr, static_cast< std::size_t >( size ), PROT_READ, MAP_PRIVATE, file.fd(), 0 ) };
	if ( mapping == MAP_FAILED )
		return std::unexpected( std::system_error( errno, std::generic_category(), "Mapping upload" ).what() );

	result.m_mapping = { static_cast< const std::byte* >( mapping ), Unmap { static_cast< std::size_t >( size ) } };
	return result;
}

std::span< const std::byte > ImportBody::bytes() const noexcept
{
	if ( m_buffer ) return { reinterpret_cast< const std::byte* >( m_buffer->data() ), m_buffer->length() };

	return { m_mapping.get(), m_mapping.get_deleter().size };
}

SHA256 ImportBody::hash() const
{
	return m_hash;
}

} // namespace idhan::httpapi
