#pragma once
#include <folly/File.h>

#include <cstdint>
#include <string>

#include "idhan/SHA256.hpp"

namespace idhan::http
{

// Completed transport upload, before content processing or MIME detection.
class TemporaryUpload
{
	folly::File m_file;
	std::uint64_t m_size;
	SHA256 m_hash;
	std::string m_filename;

  public:

	TemporaryUpload( folly::File, std::uint64_t size, SHA256 hash, std::string filename );
	~TemporaryUpload();
	TemporaryUpload( TemporaryUpload&& ) noexcept;
	TemporaryUpload& operator=( TemporaryUpload&& ) noexcept;
	TemporaryUpload( const TemporaryUpload& ) = delete;
	TemporaryUpload& operator=( const TemporaryUpload& ) = delete;
	[[nodiscard]] std::uint64_t size() const noexcept;
	[[nodiscard]] SHA256 hash() const;
	[[nodiscard]] std::string filename() const;
	[[nodiscard]] folly::File duplicateFile() const;
	[[nodiscard]] folly::File releaseFile() &&;
};

} // namespace idhan::http
