#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::server
{
struct MigrationDescriptor
{
	std::uint64_t number { 0 };
	std::filesystem::path source;
	bool transactional { true };
};

class MigrationService
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	MigrationService( db::DBContext&, std::filesystem::path migration_root );
	~MigrationService();
	MigrationService( const MigrationService& ) = delete;
	MigrationService& operator=( const MigrationService& ) = delete;

	[[nodiscard]] runtime::Task< std::vector< MigrationDescriptor > > discover() const;
	[[nodiscard]] runtime::Task<> runStartup();
	[[nodiscard]] runtime::Task<> vacuum();
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
