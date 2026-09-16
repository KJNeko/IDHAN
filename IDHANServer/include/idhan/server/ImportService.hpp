#pragma once

#include <folly/File.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"
#include "idhan/http/TemporaryUpload.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::server::modules
{
class ModuleRuntime;
}

namespace idhan::server
{
class AsyncFileService;
class ClusterService;
enum class ImportStatus : std::uint8_t
{
	imported,
	exists,
	deleted,
	rejected,
	failed
};

struct ImportResult
{
	ImportStatus status { ImportStatus::failed };
	std::optional< RecordID > record_id;
	std::string note;
};

class ImportService
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ImportService( db::DBContext&, ClusterService&, AsyncFileService&, modules::ModuleRuntime& );
	~ImportService();
	ImportService( const ImportService& ) = delete;
	ImportService& operator=( const ImportService& ) = delete;

	[[nodiscard]] runtime::Task< ImportResult > importFile( http::TemporaryUpload, bool force );
	[[nodiscard]] runtime::Task< ImportResult > importGeneratedFile(
		folly::File,
		std::uint64_t size,
		std::string filename,
		MimeID mime,
		bool force );
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
