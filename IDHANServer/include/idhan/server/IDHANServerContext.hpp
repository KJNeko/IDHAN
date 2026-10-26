#pragma once

#include <memory>

#include "AsyncFileService.hpp"
#include "AuthKeyCache.hpp"
#include "ClusterService.hpp"
#include "DownloaderPersistence.hpp"
#include "DownloaderService.hpp"
#include "Executors.hpp"
#include "ImportService.hpp"
#include "JobRuntime.hpp"
#include "MaintenanceServices.hpp"
#include "MigrationService.hpp"
#include "MimeRegistry.hpp"
#include "ModelRegistry.hpp"
#include "PeriodicServices.hpp"
#include "ServerConfig.hpp"
#include "ShutdownController.hpp"
#include "TemporaryStorage.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/runtime/DrainRegistry.hpp"

namespace idhan::server::modules
{
class ModuleRuntime;
}

namespace idhan
{
class IDHANServerContext
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	explicit IDHANServerContext( server::ServerConfig );
	~IDHANServerContext();
	IDHANServerContext( const IDHANServerContext& ) = delete;
	IDHANServerContext& operator=( const IDHANServerContext& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	void stopAdmission() noexcept;
	[[nodiscard]] runtime::Task<> drain();
	void forceStop() noexcept;

	[[nodiscard]] const server::ServerConfig& config() const noexcept;
	[[nodiscard]] server::Executors& executors() noexcept;
	[[nodiscard]] db::DBContext& database() noexcept;
	[[nodiscard]] server::ShutdownController& shutdown() noexcept;
	[[nodiscard]] server::AuthKeyCache& authKeys() noexcept;
	[[nodiscard]] server::JobRuntime& jobs() noexcept;
	[[nodiscard]] server::AsyncFileService& files() noexcept;
	[[nodiscard]] server::ClusterService& clusters() noexcept;
	[[nodiscard]] server::ImportService& imports() noexcept;
	[[nodiscard]] server::MimeRegistry& mimes() noexcept;
	[[nodiscard]] server::MigrationService& migrations() noexcept;
	[[nodiscard]] server::DownloaderPersistence& downloaderPersistence() noexcept;
	[[nodiscard]] server::DownloaderService& downloader() noexcept;
	[[nodiscard]] server::modules::ModuleRuntime& modules() noexcept;
	[[nodiscard]] server::ModelRegistry& models() noexcept;
	[[nodiscard]] server::MaintenanceServices& maintenance() noexcept;
	[[nodiscard]] server::TemporaryStorage& temporaryStorage() noexcept;
	[[nodiscard]] runtime::DrainRegistry& drainRegistry() noexcept;
};
} // namespace idhan
