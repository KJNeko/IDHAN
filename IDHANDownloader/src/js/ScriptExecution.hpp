#pragma once

#include <json/value.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <quickjs.h>
#include <string>
#include <string_view>

#include "IDHANDownloader/SessionObserver.hpp"
#include "scripts/ScriptRegistry.hpp"

namespace idhan::downloader
{
class ScriptSession;

struct PendingPromise
{
	JSContext* context {};
	JSValue resolve { JS_UNDEFINED };
	JSValue reject { JS_UNDEFINED };

	void settle( bool ok, JSValue value );
	void discard();
};

enum class ScriptStage : std::uint8_t
{
	MODULE,
	PARSER,
	DONE,
};

struct ScriptExecution
{
	ScriptSession* session {};
	std::size_t worker {};
	WorkID work_id {};
	std::optional< WorkID > parent_id {};
	std::string parser_url {};
	ScriptRoute route {};

	JSContext* context {};
	JSValue module_namespace { JS_UNDEFINED };
	JSValue result { JS_UNDEFINED };
	JSModuleDef* module {};
	ScriptStage stage { ScriptStage::MODULE };
	std::size_t outstanding {};
	bool settled {};
	bool failed {};
	std::string error {};
};

class ScriptSession
{
  public:

	virtual ~ScriptSession() = default;

	virtual FollowResult follow( ScriptExecution& execution, std::string url ) = 0;
	virtual std::optional< std::string > secret( std::string_view name ) = 0;
	virtual void startRequest( ScriptExecution& execution, Json::Value options, PendingPromise promise ) = 0;
	virtual void startImport( ScriptExecution& execution, Json::Value options ) = 0;
};

[[nodiscard]] inline ScriptExecution* executionFor( JSContext* context )
{
	return static_cast< ScriptExecution* >( JS_GetContextOpaque( context ) );
}

} // namespace idhan::downloader
