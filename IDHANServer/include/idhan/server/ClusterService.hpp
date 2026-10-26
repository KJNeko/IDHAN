#pragma once

#include <folly/File.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::db
{
class DBContext;
}

namespace idhan::server
{
class AsyncFileService;

struct ClusterInfo
{
	ClusterID id { 0 };
	std::filesystem::path root;
	std::uint64_t capacity { 0 };
	std::uint64_t free { 0 };
	bool stores_default { true };
};

class ClusterService
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ClusterService( db::DBContext&, AsyncFileService& );
	~ClusterService();
	ClusterService( const ClusterService& ) = delete;
	ClusterService& operator=( const ClusterService& ) = delete;

	[[nodiscard]] runtime::Task<> reload();
	[[nodiscard]] runtime::Task< std::vector< ClusterInfo > > list() const;
	[[nodiscard]] runtime::Task< ClusterInfo > get( ClusterID ) const;
	[[nodiscard]] runtime::Task< ClusterID > choose( std::uint64_t bytes );
	[[nodiscard]] runtime::Task< folly::File > openRecord( const SHA256&, std::string_view extension );
	[[nodiscard]] runtime::Task<> storeFile(
		ClusterID,
		const SHA256&,
		folly::File,
		std::uint64_t size,
		std::string_view extension );
	[[nodiscard]] runtime::Task<> remove( ClusterID );
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
