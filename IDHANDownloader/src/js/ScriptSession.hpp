#pragma once

#include <json/value.h>

#include <optional>
#include <string>
#include <string_view>

#include "IDHANDownloader/session/SessionObserver.hpp"
#include "PendingPromise.hpp"
#include "imports/ImportStatus.hpp"

namespace idhan::downloader
{

struct ScriptExecution;

class ScriptSession
{
  public:

	virtual ~ScriptSession() = default;

	virtual FollowResult follow( ScriptExecution&, std::string )
	{
		return FollowResult { .status = FollowStatus::FILTERED };
	}

	virtual std::expected< void, std::string > setUrlType( std::string, std::string )
	{
		return std::unexpected( "Use asynchronous URL-type persistence" );
	}

	virtual std::expected< Json::Value, std::string > secrets( std::string_view domain ) = 0;

	virtual std::expected< void, std::string > setCookie( ScriptExecution&, std::string, std::string )
	{
		return std::unexpected( "This host cannot set cookies" );
	}

	virtual std::expected< void, std::string > learnHost( ScriptExecution&, std::string, std::string )
	{
		return std::unexpected( "This host cannot learn hosts" );
	}

	virtual void startRequest( ScriptExecution& execution, Json::Value options, PendingPromise promise ) = 0;
	virtual std::shared_ptr< ImportStatus > startImport( ScriptExecution& execution, Json::Value options ) = 0;

	virtual void startDelay( ScriptExecution&, std::uint32_t, PendingPromise promise )
	{
		promise.resolveOrReject( false, JS_NewString( promise.context, "This host cannot delay execution" ) );
	}

	virtual void startImportDirect( ScriptExecution&, Json::Value, std::string, PendingPromise promise )
	{
		promise.resolveOrReject( false, JS_NewString( promise.context, "This host does not support direct imports" ) );
	}

	// Database-backed operations suspend their promise instead of blocking the script worker.
	virtual void startFollow(
		ScriptExecution&,
		std::string,
		std::optional< std::string >,
		Json::Value,
		PendingPromise promise )
	{
		promise.resolveOrReject(
			false, JS_NewString( promise.context, "This host does not support asynchronous following" ) );
	}

	virtual void startSetUrlType( ScriptExecution&, std::string, std::string, PendingPromise promise )
	{
		promise.resolveOrReject(
			false, JS_NewString( promise.context, "This host cannot persist URL types asynchronously" ) );
	}

	virtual void startLearnHost( ScriptExecution&, std::string, std::string, PendingPromise promise )
	{
		promise.resolveOrReject(
			false, JS_NewString( promise.context, "This host cannot learn hosts asynchronously" ) );
	}

	// Called only when the script explicitly asks to wait for its queued import.
	virtual void awaitImport( ScriptExecution&, std::shared_ptr< ImportStatus >, PendingPromise promise )
	{
		promise.resolveOrReject( false, JS_NewString( promise.context, "This host does not support awaited imports" ) );
	}
};

} // namespace idhan::downloader
