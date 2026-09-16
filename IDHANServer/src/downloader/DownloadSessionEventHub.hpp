#pragma once

#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/SSEEventHub.hpp"

namespace idhan::downloader
{

class DownloadSessionEventHub
{
	http::SSEEventHub m_events {};

  public:

	static DownloadSessionEventHub& instance();

	//! \param sessions Restricts the subscription to these sessions; empty subscribes to every one.
	//! \param existing Sessions reported in the opening snapshot, so a reconnecting client sees the
	//!        ones already in progress without waiting for one of them to change.
	[[nodiscard]] http::SSESubscription subscribe(
		std::vector< DownloadSessionID > sessions,
		const std::vector< DownloadSessionID >& existing );
	void shutdown();

	void notify( DownloadSessionID session_id );
	void notifyRemoved( DownloadSessionID session_id );

};

} // namespace idhan::downloader
