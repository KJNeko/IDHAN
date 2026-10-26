#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <quickjs.h>
#include <string>

#include "IDHANDownloader/SessionObserver.hpp"
#include "scripts/ScriptRegistry.hpp"

namespace idhan::downloader
{
class ScriptSession;

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

[[nodiscard]] inline ScriptExecution* executionFor( JSContext* context )
{
	return static_cast< ScriptExecution* >( JS_GetContextOpaque( context ) );
}

} // namespace idhan::downloader
