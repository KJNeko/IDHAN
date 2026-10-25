#pragma once

#include <json/value.h>

#include <optional>
#include <string>
#include <string_view>

#include "IDHANDownloader/SessionObserver.hpp"
#include "PendingPromise.hpp"

namespace idhan::downloader
{

struct ScriptExecution;

class ScriptSession
{
  public:

	virtual ~ScriptSession() = default;

	virtual FollowResult follow( ScriptExecution& execution, std::string url ) = 0;
	virtual std::expected< void, std::string > setUrlType( std::string url, std::string type ) = 0;
	virtual std::optional< std::string > secret( std::string_view name ) = 0;
	virtual void startRequest( ScriptExecution& execution, Json::Value options, PendingPromise promise ) = 0;
	virtual void startImport( ScriptExecution& execution, Json::Value options ) = 0;
};

} // namespace idhan::downloader
