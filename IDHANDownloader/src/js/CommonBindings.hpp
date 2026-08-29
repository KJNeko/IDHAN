#pragma once

#include <optional>
#include <string>

#include "quickjs.h"

namespace idhan::downloader::quickjs
{

inline std::optional< std::string > toString( JSContext* context, const JSValueConst value )
{
	std::size_t size {};
	const char* text { JS_ToCStringLen( context, &size, value ) };

	if ( text == nullptr ) return std::nullopt;

	std::string result { text, size };
	JS_FreeCString( context, text );
	return result;
}

} // namespace idhan::downloader::quickjs
