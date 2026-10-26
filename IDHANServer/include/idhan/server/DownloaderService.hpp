#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "IDHANDownloader/DownloaderConfig.hpp"
#include "IDHANDownloader/LaneSnapshot.hpp"
#include "IDHANDownloader/SessionSnapshot.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
class DownloaderPersistence;

class DownloaderService
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	DownloaderService( downloader::DownloaderConfig, DownloaderPersistence& );
	~DownloaderService();
	DownloaderService( const DownloaderService& ) = delete;
	DownloaderService& operator=( const DownloaderService& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] runtime::Task<> restoreSessions();
	[[nodiscard]] std::vector< downloader::LaneSnapshot > lanes() const;
	[[nodiscard]] std::vector< downloader::SessionSnapshot > sessions( std::uint64_t since = 0 ) const;
	void resetBackoff( std::string_view lane );
	void resetAllBackoff();
	void stopProducers() noexcept;
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
