#include "idhan/http/TemporaryUpload.hpp"

#include <utility>

namespace idhan::http
{

TemporaryUpload::TemporaryUpload( folly::File file, const std::uint64_t size, SHA256 hash, std::string filename ) :
  m_file( std::move( file ) ),
  m_size( size ),
  m_hash( std::move( hash ) ),
  m_filename( std::move( filename ) )
{}

TemporaryUpload::~TemporaryUpload() = default;

TemporaryUpload::TemporaryUpload( TemporaryUpload&& ) noexcept = default;

TemporaryUpload& TemporaryUpload::operator=( TemporaryUpload&& ) noexcept = default;

std::uint64_t TemporaryUpload::size() const noexcept
{
	return m_size;
}

SHA256 TemporaryUpload::hash() const
{
	return m_hash;
}

std::string TemporaryUpload::filename() const
{
	return m_filename;
}

folly::File TemporaryUpload::duplicateFile() const
{
	return m_file.dupCloseOnExec();
}

folly::File TemporaryUpload::releaseFile() &&
{
	return std::move( m_file );
}

} // namespace idhan::http
