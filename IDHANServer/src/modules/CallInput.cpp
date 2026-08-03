//
// Created by kj16609 on 8/2/26.
//

#include "CallInput.hpp"

#include <sys/stat.h>

#include <fcntl.h>
#include <format>

namespace idhan::modules
{

std::expected< CallInput, std::string > CallInput::forPath( const std::filesystem::path& path )
{
	ipc::UniqueFd file { ::open( path.c_str(), O_RDONLY | O_CLOEXEC ) };
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

int CallInput::fd() const
{
	// Exactly one of the two is ever populated: a file input has no blob, and a staged input's
	// descriptor belongs to the blob that owns the mapping the server reads.
	return m_file ? m_file.get() : m_blob.fd();
}

} // namespace idhan::modules
