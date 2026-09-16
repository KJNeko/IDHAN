#pragma once
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/ssl/OpenSSLHash.h>

#include "HTTPConfig.hpp"
#include "RequestContext.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::http
{
class UploadPipeline
{
	std::filesystem::path m_temp_path;
	folly::IOBufQueue m_buffer { folly::IOBufQueue::cacheChainLength() };
	folly::File m_file {};
	folly::ssl::OpenSSLHash::Digest m_digest {};
	std::uint64_t m_size {};
	bool m_finished { false };
	void write( const folly::IOBuf& );

  public:

	explicit UploadPipeline( std::filesystem::path temp_path );
	~UploadPipeline() = default;
	[[nodiscard]] runtime::Task<> append( std::unique_ptr< folly::IOBuf > );
	[[nodiscard]] runtime::Task< RequestBody > finish();
};
} // namespace idhan::http
