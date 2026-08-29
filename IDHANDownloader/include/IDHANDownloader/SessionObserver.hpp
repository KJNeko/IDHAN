#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../../../IDHAN/include/IDHANTypes.hpp"

namespace idhan::downloader
{

using WorkID = std::uint64_t;

enum class FollowStatus
{
	QUEUED,
	FILTERED,
	ALREADY_QUEUED,
	ALREADY_EXPLORED,
	ALREADY_IMPORTED,
};

struct FollowResult
{
	FollowStatus status {};
	WorkID work_id {};
	RecordID record_id {};
};

struct WorkInfo
{
	WorkID id {};
	std::optional< WorkID > parent {};
	std::string url {};
};

struct RequestInfo
{
	WorkID work {};
	std::string url {};
	std::string lane {};
	std::int32_t status {};
	std::size_t bytes {};
};

struct ImportInfo
{
	WorkID work {};
	std::string url {};
	RecordID record_id {};
	std::size_t size {};
	std::string content_type {};
	std::string filename {};
	std::string note {};
};

//! Callbacks may arrive concurrently. Ordering is guaranteed only within one work item.
class SessionObserver
{
  public:

	virtual ~SessionObserver() = default;

	virtual void onStarted( const WorkInfo& ) {}

	virtual void onCompleted( const WorkInfo& ) {}

	virtual void onFailed( const WorkInfo&, const std::string& ) {}

	virtual void onRequest( const RequestInfo& ) {}

	virtual void onImported( const ImportInfo& ) {}

	virtual void onImportFailed( const WorkInfo&, const std::string& /* url */, const std::string& /* error */ ) {}

	//! WorkInfo describes the followed URL; id is 0 unless it was queued.
	virtual void onFollowed( const WorkInfo&, FollowStatus ) {}

	//! Fires on every transition to idle.
	virtual void onFinished() {}

	virtual std::optional< std::int64_t > alreadyImported( const std::string& ) { return std::nullopt; }
};

} // namespace idhan::downloader
