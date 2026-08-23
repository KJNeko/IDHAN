#include "CallInput.hpp"

#include <sys/stat.h>

#include <fcntl.h>
#include <format>

namespace idhan::modules
{

std::expected< CallInput, std::string > CallInput::forPath( const std::filesystem::path& path )
{
	if ( !std::filesystem::is_regular_file( path ) )
		return std::unexpected( std::format( "{} is not a regular file", path.string() ) );

	ipc::UniqueFd file { ::open( path.c_str(), O_RDONLY | O_CLOEXEC /* prevents child inheritence */ ) };
	if ( !file ) return std::unexpected( std::format( "could not open {}", path.string() ) );

	struct stat info {};
	if ( ::fstat( file.get(), &info ) != 0 )
		return std::unexpected( std::format( "could not stat {}", path.string() ) );

	// A directory or a device would map to something the size no longer describes.
	if ( !S_ISREG( info.st_mode ) ) return std::unexpected( std::format( "{} is not a regular file", path.string() ) );

	CallInput input {};
	input.m_size = static_cast< std::size_t >( info.st_size );
	input.m_file = std::move( file );

	return input;
}

std::expected< CallInput, std::string > CallInput::forBlob( ipc::Blob blob )
{
	CallInput input {};
	input.m_size = blob.size();
	input.m_blob = std::move( blob );

	return input;
}

std::expected< CallInput, std::string > CallInput::forBytes( const std::span< const std::byte > bytes )
{
	auto blob { ipc::Blob::fromBytes( bytes ) };

	if ( !blob ) return std::unexpected( std::move( blob.error() ) );

	return forBlob( std::move( *blob ) );
}

std::expected< std::shared_ptr< const CallInput >, std::string > CallInput::sharedForBytes(
	const std::span< const std::byte > bytes )
{
	auto input { forBytes( bytes ) };

	if ( !input ) return std::unexpected( std::move( input.error() ) );

	return std::make_shared< const CallInput >( std::move( *input ) );
}

int CallInput::fd() const
{
	return m_file ? m_file.get() : m_blob.fd();
}

} // namespace idhan::modules
