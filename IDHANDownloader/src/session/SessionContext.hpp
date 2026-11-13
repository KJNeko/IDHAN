#pragma once

#include <folly/coro/Baton.h>
#include <json/value.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "IDHANDownloader/DownloaderContext.hpp"
#include "IDHANDownloader/session/SessionContext.hpp"
#include "cookies/CookieStore.hpp"
#include "http/LanePool.hpp"
#include "http/Transfer.hpp"
#include "js/ScriptActivity.hpp"
#include "js/ScriptExecution.hpp"
#include "js/ScriptRunner.hpp"
#include "js/ScriptSession.hpp"
#include "scripts/ScriptRegistry.hpp"
#include "session/SessionDiagnostics.hpp"

namespace idhan::downloader
{
class ScriptContext;
void mergeFollowTags( Json::Value& tags, const Json::Value& inherited );

struct SessionEnvironment
{
	ScriptRegistry* registry {};
	LanePool* lanes {};
	CookieStore* cookies {};
	ImportSinkFactory* imports {};
	SecretProvider* secrets {};
	ScriptRunner* runner {};
	folly::CPUThreadPoolExecutor* import_executor {};
	DownloaderConfig config {};
};

// Session bookkeeping is shared by independently scheduled parser runtimes.
class SessionContext::Impl final : public ScriptSession
{
	struct WorkRecord
	{
		std::optional< WorkID > parent {};
		std::string url {};
		std::string url_class {};
		std::string parser {};
		WorkPhase phase {};
		std::size_t outstanding {};
		bool awaiting_result {};
		std::chrono::steady_clock::time_point started_at {};
	};

	struct Pending
	{
		std::shared_ptr< NetworkActivity::Request > progress { std::make_shared< NetworkActivity::Request >() };
		WorkID work {};
		PendingPromise promise {};
		std::string url {};
		std::string lane {};
		std::string response_type {};
		std::vector< std::string > sensitive_query {};
		Json::Value import_options {};
		bool import {};
		bool import_completed {};
		bool existing_import {};
		bool host {};
		bool ready {};
		std::expected< void, std::string > host_result_e {};
		bool allow_http_errors {};
		//! The execution remains valid until its runtime lease holder settles or abandons it.
		std::size_t runtime {};
		ScriptExecution* execution {};
		TransferResult result { TransferResponse {} };
		std::chrono::steady_clock::time_point started_at {};
		std::shared_ptr< ImportStatus > import_status {};
	};

	struct DirectImport
	{
		std::unique_ptr< ImportSink > sink;
		WorkID work {};
		std::string url;
		ImportMetadata metadata;
		std::optional< std::int64_t > posted_at {};
	};

	// Only the producer's host lane touches its streams; m_mutex guards the map shared by all lanes.
	std::map< std::pair< WorkID, std::uint64_t >, DirectImport > m_direct_imports {};
	folly::coro::Task< std::expected< void, std::string > > importDirectOnHost(
		WorkID owner,
		std::string source,
		Json::Value options,
		std::string chunk );
	folly::coro::Task< void > abortDirectImports( WorkID owner );

	SessionEnvironment m_environment;
	SessionOptions m_options;
	std::atomic_size_t m_script_worker {};
	std::map< std::size_t, std::shared_ptr< ScriptActivity > > m_script_activities {};
	SessionDiagnostics m_diagnostics {};
	std::shared_ptr< NetworkActivity > m_network { std::make_shared< NetworkActivity >() };

	mutable std::mutex m_mutex {};
	std::atomic< std::shared_ptr< folly::coro::Baton > > m_changed { std::make_shared< folly::coro::Baton >() };
	CookieOverlay m_overlay {};
	std::unordered_set< std::string > m_seen {};
	std::unordered_map< WorkID, WorkRecord > m_records {};
	std::unordered_map< std::uint64_t, Pending > m_pending {};
	std::size_t m_queued {};
	std::size_t m_inflight_requests {};
	std::size_t m_import_tasks {};
	std::size_t m_import_preparations {};
	using HostTask = folly::coro::Task< void >;
	//! Session-wide host work that belongs to no single producer.
	static constexpr WorkID session_lane { 0 };
	//! Host work runs in order within its producer's lane; separate lanes run concurrently.
	//! A lane exists only while its runner is active.
	std::map< WorkID, std::deque< HostTask > > m_host_lanes {};
	std::size_t m_host_tasks {};
	bool m_finish_notification {};

	struct ImportPreparation
	{
		WorkID work {};
		std::uint64_t pending {};
		WorkID parent {};
		Json::Value options {};
	};

	std::deque< ImportPreparation > m_import_queue {};
	//! Reserved starts count against the in-flight budget.
	std::size_t m_starting {};
	std::uint64_t m_next_pending { 1 };
	//! Prevents duplicate drain notifications until new work arrives.
	bool m_drained_reported { true };

	std::atomic< WorkID > m_next_work { 1 };
	std::atomic_size_t m_outstanding {};
	std::atomic_bool m_cancelled {};
	std::atomic_bool m_closed {};
	std::atomic_bool m_shutting_down {};
	std::shared_ptr< std::atomic_bool > m_transfer_cancelled { std::make_shared< std::atomic_bool >() };

	std::weak_ptr< SessionContext > m_self {};

	[[nodiscard]] bool enqueue(
		WorkID id,
		std::optional< WorkID > parent,
		std::string url,
		ScriptRoute route,
		Json::Value tags = Json::Value { Json::objectValue } );
	void retire( WorkID id );
	//! Reports only the transition to idle.
	void reportDrainedIfIdle();
	[[nodiscard]] bool isIdleLocked() const;
	[[nodiscard]] bool isDrainedLocked() const;
	HostTask notifyFinished();
	[[nodiscard]] std::expected< void, std::string > postHostTask( WorkID owner, HostTask task );
	// Requires m_mutex. Scheduling never waits for the callback to execute.
	[[nodiscard]] std::expected< void, std::string > postHostTaskLocked( WorkID owner, HostTask task );
	//! Runs host work that needs no ordering outside every lane.
	[[nodiscard]] std::expected< void, std::string > spawnHostTask( HostTask task );
	HostTask drainHostLane( WorkID owner );
	HostTask runHostTask( HostTask task );
	static HostTask runHostLane( std::shared_ptr< SessionContext > handle, WorkID owner );
	static HostTask runDetachedHostTask( std::shared_ptr< SessionContext > handle, HostTask task );
	using ObserverCallback = std::function< HostTask( SessionObserver& ) >;
	void notifyObserver( WorkID owner, ObserverCallback callback );
	HostTask runObserver( ObserverCallback callback );
	HostTask runHostOperation( std::uint64_t id, folly::coro::Task< std::expected< void, std::string > > operation );
	enum class HostOrder : std::uint8_t
	{
		//! Runs in the producer's lane, after its earlier callbacks and operations.
		PRODUCER,
		//! Runs immediately; for operations that touch no ordered state.
		UNORDERED,
	};

	void startHostOperation(
		ScriptExecution& execution,
		PendingPromise promise,
		folly::coro::Task< std::expected< void, std::string > > operation,
		HostOrder order = HostOrder::PRODUCER );
	void deliverHost( std::uint64_t id, std::expected< void, std::string > result );
	[[nodiscard]] folly::coro::Task< std::expected< void, std::string > > followOnHost(
		WorkID parent,
		std::string url,
		std::optional< std::string > type,
		Json::Value tags );
	HostTask stageImport( ImportPreparation preparation );
	HostTask reportFollowed( WorkInfo info, FollowStatus status );
	folly::coro::Task< std::expected< void, std::string > > setUrlTypeOnHost( std::string url, std::string type );
	//! The normalized URL, or the URL unchanged when it cannot be parsed, for display and duplicate checks.
	[[nodiscard]] std::string comparableUrl( std::string_view url ) const;
	folly::coro::Task< std::expected< void, std::string > > learnHostOnHost(
		std::string source,
		std::string origin,
		std::string url_class );
	void dispatchImports();
	void completeImport( std::uint64_t id, std::expected< void, std::string > result_e, bool existing = false );
	static HostTask prepareImport( std::shared_ptr< SessionContext > handle, ImportPreparation preparation );
	static HostTask completeDetachedImport( std::shared_ptr< SessionContext > handle, std::uint64_t id );
	void finishImportTask( bool preparation = true );
	//! Routes a result to the realm's worker.
	void deliver( std::uint64_t id, TransferResult result );
	[[nodiscard]] std::uint64_t track( ScriptExecution& execution, Pending pending );

	void callParser( ScriptContext& scripts, ScriptExecution& execution );
	void updateWorkState( const ScriptExecution& execution );
	folly::coro::Task< std::expected< void, std::string > > startImportImpl( ImportPreparation preparation );
	void finishWork( ScriptExecution& execution );
	void failWork( ScriptExecution& execution, std::string error );

	[[nodiscard]] static RequestTiming timingOf( const Pending& pending );
	void completeRequest( Pending& pending );
	folly::coro::Task< std::expected< void, std::string > > settleImport( Pending pending );
	void rejectPending( Pending& pending, const std::string& error );
	void reportImportFailure(
		WorkID work,
		const std::string& url,
		const std::string& error,
		std::optional< WorkID > parent = std::nullopt,
		FailureStage stage = FailureStage::IMPORT );
	void attachTransferListener(
		const std::shared_ptr< NetworkActivity::Request >& progress,
		WorkID work,
		bool import );
	void reportRequestFailure( const RequestFailure& failure );
	[[nodiscard]] std::expected< TransferRequest, std::string > buildRequest(
		const Json::Value& options,
		std::string& response_type );

	[[nodiscard]] static WorkInfo infoFor( const ScriptExecution& execution );

  public:

	Impl( SessionEnvironment environment, SessionOptions options );
	Impl( const Impl& ) = delete;
	Impl& operator=( const Impl& ) = delete;
	~Impl() override;

	void adopt( const std::shared_ptr< SessionContext >& self );

	[[nodiscard]] std::expected< WorkID, std::string > submit(
		std::string url,
		std::optional< WorkID > parent,
		std::function< void( WorkID ) > on_reserved );
	void cancel();
	folly::coro::Task< void > wait();
	folly::coro::Task< void > drain();
	void notifyWaiters();
	void close();
	// Preserve unfinished host rows while shutdown drains native callbacks.
	void shutdown();
	[[nodiscard]] bool idle() const;
	[[nodiscard]] std::size_t outstanding() const;

	[[nodiscard]] std::uint64_t downloadedBytes() const { return m_network->total(); }

	[[nodiscard]] SessionSnapshot snapshot( std::uint64_t since ) const;

	[[nodiscard]] const std::string& rootUrl() const { return m_options.root_url; }

	//! Claims a start slot without dequeuing sessions at their in-flight limit.
	[[nodiscard]] bool reserveStart();
	//! Called while holding the runner queue lock.
	[[nodiscard]] bool acceptsWork() const;

	void attachRuntime( std::size_t runtime, std::shared_ptr< ScriptActivity > activity );

	void releaseStart();
	void releaseStartIfReady( ScriptExecution& execution );
	//! Returns null after reporting a terminal startup failure.
	[[nodiscard]] std::unique_ptr< ScriptExecution > beginWork(
		ScriptContext& scripts,
		ScriptWork& work,
		std::size_t runtime );
	[[nodiscard]] bool step( ScriptContext& scripts, ScriptExecution& execution );
	void releaseWork( ScriptContext& scripts, ScriptExecution& execution );
	void completePendingOperation( std::uint64_t id );
	//! Discards realm-bound promises; imports complete independently.
	void abandon( ScriptExecution& execution );
	void retireQueued( WorkID id );

	std::expected< Json::Value, std::string > secrets( std::string_view domain ) override;
	std::expected< void, std::string > setCookie( ScriptExecution&, std::string, std::string ) override;
	void startFollow(
		ScriptExecution&,
		std::string url,
		std::optional< std::string > type,
		Json::Value tags,
		PendingPromise ) override;
	void startSetUrlType( ScriptExecution&, std::string url, std::string type, PendingPromise ) override;
	void startLearnHost( ScriptExecution&, std::string origin, std::string url_class, PendingPromise ) override;
	void startRequest( ScriptExecution& execution, Json::Value options, PendingPromise promise ) override;
	folly::coro::Task< std::expected< void, std::string > > delayOnHost( std::uint32_t milliseconds );
	void startDelay( ScriptExecution&, std::uint32_t, PendingPromise ) override;
	void startImportDirect( ScriptExecution&, Json::Value, std::string, PendingPromise ) override;
	std::shared_ptr< ImportStatus > startImport( ScriptExecution& execution, Json::Value options ) override;
	void awaitImport( ScriptExecution& execution, std::shared_ptr< ImportStatus > status, PendingPromise promise )
		override;
};

} // namespace idhan::downloader
