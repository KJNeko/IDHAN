#pragma once

#include <json/value.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "IDHANDownloader/DownloaderContext.hpp"
#include "IDHANDownloader/SessionContext.hpp"
#include "SessionDiagnostics.hpp"
#include "cookies/CookieStore.hpp"
#include "http/LanePool.hpp"
#include "http/Transfer.hpp"
#include "js/ScriptExecution.hpp"
#include "js/ScriptRunner.hpp"
#include "scripts/ScriptRegistry.hpp"

namespace idhan::downloader
{
class ScriptContext;

struct SessionEnvironment
{
	ScriptRegistry* registry {};
	LanePool* lanes {};
	CookieStore* cookies {};
	ImportSinkFactory* imports {};
	SecretProvider* secrets {};
	ScriptRunner* runner {};
	DownloaderConfig config {};
};

//! Work may run concurrently on any ScriptRunner worker.
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
		std::chrono::steady_clock::time_point started_at {};
	};

	struct Pending
	{
		WorkID work {};
		PendingPromise promise {};
		std::string url {};
		std::string lane {};
		std::string response_type {};
		std::vector< std::string > sensitive_query {};
		Json::Value import_options {};
		bool import {};
		//! The execution remains valid until its owning worker settles or abandons it.
		std::size_t worker {};
		ScriptExecution* execution {};
		TransferResult result { TransferResponse {} };
		std::chrono::steady_clock::time_point started_at {};
	};

	SessionEnvironment m_environment;
	SessionOptions m_options;
	SessionDiagnostics m_diagnostics {};

	mutable std::mutex m_mutex {};
	std::condition_variable m_drained {};
	CookieOverlay m_overlay {};
	std::unordered_set< std::string > m_seen {};
	std::unordered_map< WorkID, WorkRecord > m_records {};
	std::unordered_map< std::uint64_t, Pending > m_pending {};
	std::size_t m_queued {};
	std::size_t m_inflight_requests {};
	//! Reserved starts count against the in-flight budget.
	std::size_t m_starting {};
	std::uint64_t m_next_pending { 1 };
	//! Prevents duplicate drain notifications until new work arrives.
	bool m_drained_reported {};

	std::atomic< WorkID > m_next_work { 1 };
	std::atomic_size_t m_outstanding {};
	std::atomic_bool m_cancelled {};
	std::atomic_bool m_closed {};
	std::shared_ptr< std::atomic_bool > m_transfer_cancelled { std::make_shared< std::atomic_bool >() };

	std::weak_ptr< SessionContext > m_self {};

	[[nodiscard]] bool enqueue( WorkID id, std::optional< WorkID > parent, std::string url );
	void retire( WorkID id );
	//! Reports only the transition to idle.
	void reportDrainedIfIdle();
	void publishDiagnostics();
	//! Routes a result to the realm's worker.
	void deliver( std::uint64_t id, TransferResult result );
	[[nodiscard]] std::uint64_t track( ScriptExecution& execution, Pending pending );

	void callParser( ScriptContext& scripts, ScriptExecution& execution );
	void finishWork( ScriptExecution& execution );
	void failWork( ScriptExecution& execution, std::string error );

	void settleRequest( Pending& pending );
	void settleImport( Pending& pending );
	void rejectPending( Pending& pending, const std::string& error );
	void reportImportFailure( WorkID work, const std::string& url, const std::string& error );
	[[nodiscard]] std::expected< TransferRequest, std::string > buildRequest(
		const Json::Value& options,
		std::string& response_type );

	void loadManifestCookies( ScriptExecution& execution );
	[[nodiscard]] JSValue buildFlags( ScriptExecution& execution ) const;
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
	void wait();
	void close();
	[[nodiscard]] bool idle() const;
	[[nodiscard]] std::size_t outstanding() const;
	[[nodiscard]] SessionSnapshot snapshot( std::uint64_t since ) const;

	[[nodiscard]] const std::string& rootUrl() const { return m_options.root_url; }

	//! Claims a start slot without dequeuing sessions at their in-flight limit.
	[[nodiscard]] bool reserveStart();
	//! Called while holding the runner queue lock.
	[[nodiscard]] bool acceptsWork() const;
	void releaseStart();
	//! Returns null after reporting a terminal startup failure.
	[[nodiscard]] std::unique_ptr< ScriptExecution > beginWork(
		ScriptContext& scripts,
		ScriptWork& work,
		std::size_t worker );
	[[nodiscard]] bool step( ScriptContext& scripts, ScriptExecution& execution );
	void releaseWork( ScriptContext& scripts, ScriptExecution& execution );
	void settle( std::uint64_t id );
	//! Discards realm-bound promises; imports settle independently.
	void abandon( ScriptExecution& execution );
	void retireQueued( WorkID id );

	FollowResult follow( ScriptExecution& execution, std::string url ) override;
	std::optional< std::string > secret( std::string_view name ) override;
	void startRequest( ScriptExecution& execution, Json::Value options, PendingPromise promise ) override;
	void startImport( ScriptExecution& execution, Json::Value options ) override;
};

} // namespace idhan::downloader
