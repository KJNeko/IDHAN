#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <thread>

namespace idhan::downloader
{
class ScriptRegistry;

class ScriptWatcher
{
	ScriptRegistry& m_registry;
	std::filesystem::path m_root;
	std::filesystem::path m_url_classes;
	std::chrono::milliseconds m_interval;
	std::chrono::milliseconds m_debounce;
	std::jthread m_thread;

	struct FileStamp
	{
		std::uintmax_t size {};
		std::filesystem::file_time_type modified {};
		bool operator==( const FileStamp& ) const = default;
	};

	using Snapshot = std::map< std::filesystem::path, FileStamp >;
	[[nodiscard]] std::expected< Snapshot, std::string > scan() const;
	void run( std::stop_token stop );

  public:

	ScriptWatcher(
		ScriptRegistry& registry,
		std::filesystem::path root,
		std::filesystem::path url_classes = {},
		std::chrono::milliseconds interval = std::chrono::milliseconds { 500 },
		std::chrono::milliseconds debounce = std::chrono::milliseconds { 250 } );
	~ScriptWatcher();
};
} // namespace idhan::downloader
