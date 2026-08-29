#pragma once

#include <json/value.h>

#include <expected>
#include <optional>
#include <quickjs.h>
#include <string>

namespace idhan::downloader
{

[[nodiscard]] std::expected< void, std::string > installIdhanBindings( JSContext* context );

[[nodiscard]] std::string scriptValueString( JSContext* context, JSValueConst value );
[[nodiscard]] std::string scriptErrorString( JSContext* context, JSValueConst value );
[[nodiscard]] std::string scriptExceptionString( JSContext* context );
[[nodiscard]] std::expected< Json::Value, std::string > scriptValueToJson( JSContext* context, JSValueConst value );

} // namespace idhan::downloader
