#pragma once

#include <folly/coro/Task.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../../../IDHAN/include/IDHANTypes.hpp"
#include "IDHANDownloader/http/RequestTiming.hpp"
#include "IDHANDownloader/http/TransferEvent.hpp"

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
	//! What ran the work; empty until a URL class has claimed it.
	std::string url_class {};
	std::string handler {};
	std::string script {};
};

struct RequestInfo
{
	WorkID work {};
	std::string url {};
	std::string lane {};
	std::int32_t status {};
	std::size_t bytes {};
	RequestTiming timing {};
};

struct RequestFailure
{
	WorkID work {};
	std::string url {};
	std::string lane {};
	//! Unset when the transfer never produced a response.
	std::optional< std::int32_t > status {};
	std::string message {};
	RequestTiming timing {};
};

//! Where work gave up, so a host can label the failure without parsing its message.
enum class FailureStage : std::uint8_t
{
	//! No URL class accepted the URL.
	ROUTE,
	//! A request the work depended on failed or answered with an error.
	DOWNLOAD,
	PARSER,
	IMPORT,
	CLOSED,
};

struct WorkFailure
{
	std::string message {};
	FailureStage stage { FailureStage::PARSER };
};

struct ImportInfo
{
	//! Matches onImportQueued; it is the import identity, not the parser identity.
	WorkID work {};
	std::string url {};
	RecordID record_id {};
	std::size_t size {};
	std::string content_type {};
	std::string filename {};
	std::string note {};
	//! The file already matched a record, so nothing new was stored.
	bool existing {};
	//! Left default when an existing record was reused without a transfer.
	RequestTiming timing {};
	std::optional< std::int64_t > posted_at {};
};

//! Callbacks run on host/import executors. Database-backed callbacks suspend. Lifecycle callbacks are
//! ordered per work, and a work's imports follow its start; callbacks for different works, and import
//! preparation/completion, can run concurrently.
class SessionObserver
{
  public:

	virtual ~SessionObserver() = default;

	//! Hooks own arguments across suspension. Implementations await persistence directly.
	virtual folly::coro::Task< void > onStarted( WorkInfo ) { co_return; }

	virtual folly::coro::Task< void > onCompleted( WorkInfo ) { co_return; }

	virtual folly::coro::Task< void > onFailed( WorkInfo, std::string ) { co_return; }

	virtual folly::coro::Task< void > onWorkFailed( WorkInfo info, WorkFailure failure )
	{
		co_await onFailed( std::move( info ), std::move( failure.message ) );
	}

	virtual folly::coro::Task< void > onImportFailure( WorkInfo info, std::string url, WorkFailure failure )
	{
		co_await onImportFailed( std::move( info ), std::move( url ), std::move( failure.message ) );
	}

	//! Network-thread accounting only; implementations must not perform I/O or wait.
	virtual void onTransfer( WorkID, TransferEvent, bool /* import */ ) {}

	virtual folly::coro::Task< void > onRequest( RequestInfo ) { co_return; }

	virtual folly::coro::Task< void > onRequestFailed( RequestFailure ) { co_return; }

	virtual folly::coro::Task< void > onImported( ImportInfo ) { co_return; }

	virtual folly::coro::Task< void > onImportFailed( WorkInfo, std::string, std::string ) { co_return; }

	virtual folly::coro::Task< void > onFollowed( WorkInfo, FollowStatus ) { co_return; }

	//! Fires on every transition to idle.
	virtual folly::coro::Task< void > onFinished() { co_return; }

	//! An error prevents the import from opening a sink or dispatching HTTP.
	virtual folly::coro::Task< std::expected< void, std::string > > onImportQueued( WorkInfo )
	{
		co_return std::expected< void, std::string > {};
	}

	virtual folly::coro::Task< std::expected< void, std::string > > onUrlTypeSet( std::string, std::string )
	{
		co_return std::expected< void, std::string > {};
	}

	virtual folly::coro::Task< std::expected< void, std::string > > onUrlTypesSet(
		std::vector< std::pair< std::string, std::string > > types )
	{
		for ( auto& [ url, type ] : types )
		{
			const auto saved_e { co_await onUrlTypeSet( std::move( url ), std::move( type ) ) };
			if ( !saved_e.has_value() ) co_return std::unexpected( saved_e.error() );
		}

		co_return std::expected< void, std::string > {};
	}

	virtual folly::coro::Task< std::optional< RecordID > > alreadyImported( std::string ) { co_return std::nullopt; }
};

} // namespace idhan::downloader
