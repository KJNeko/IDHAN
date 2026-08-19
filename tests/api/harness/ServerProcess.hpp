#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace idhan::test
{

//! An IDHANServer of our own: its own port, its own temp path, its own schema.
class ServerProcess
{
	std::filesystem::path m_temp_path;
	std::filesystem::path m_log_path { m_temp_path / "server.log" };
	std::string m_schema;
	std::uint16_t m_port;
	pid_t m_pid { -1 };

	void waitUntilReady() const;

	//! The end of the server's own log, for when it never came up.
	[[nodiscard]] std::string logTail() const;

  public:

	ServerProcess( std::string schema );

	ServerProcess( const ServerProcess& ) = delete;
	ServerProcess& operator=( const ServerProcess& ) = delete;

	~ServerProcess();

	//! Ends the server and waits for it to go. Doing this twice is harmless.
	void stop();

	[[nodiscard]] std::uint16_t port() const { return m_port; }

	[[nodiscard]] const std::string& schema() const { return m_schema; }
};

//! An unused TCP port, as of the moment it was asked for.
[[nodiscard]] std::uint16_t findFreePort();

} // namespace idhan::test
