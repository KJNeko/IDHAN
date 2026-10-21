#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>

#include "idhan/db/DBConfig.hpp"
#include "idhan/http/HTTPConfig.hpp"
#include "modules/ModuleRuntime.hpp"

namespace idhan::server
{
struct ConfigInputs
{
	int argc { 0 };
	char* const * argv { nullptr };
	std::optional< std::filesystem::path > local_config { std::filesystem::path { "./config.toml" } };
	std::optional< std::filesystem::path > user_config;
	std::optional< std::filesystem::path > system_config;
};

struct ServerConfig
{
	http::HTTPConfig http;
	db::DBConfig database;
	modules::ModuleRuntimeConfig modules;
	std::filesystem::path webui_root;
	std::filesystem::path module_root;
	std::chrono::milliseconds shutdown_timeout { 300000 };
	std::chrono::milliseconds job_cleanup_interval { 10000 };
	std::chrono::milliseconds sse_heartbeat_interval { 10000 };
	bool run_startup_migrations { true };
	bool purge_thumbnails_on_startup { false };

	[[nodiscard]] static ServerConfig defaults();
	// Precedence is CLI > environment > local TOML > user TOML > system TOML.
	[[nodiscard]] static ServerConfig load( ConfigInputs );
	[[nodiscard]] std::size_t ioThreadCount( std::size_t hardware_concurrency ) const;
	[[nodiscard]] std::size_t applicationThreadCount( std::size_t hardware_concurrency ) const;
	[[nodiscard]] std::size_t heavyThreadCount( std::size_t hardware_concurrency ) const;
	[[nodiscard]] std::size_t databaseThreadCount( std::size_t hardware_concurrency ) const;
	void validate() const;
};
} // namespace idhan::server
