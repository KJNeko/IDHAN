#pragma once
#include <expected>
#include <string>

#include "quickjs.h"

namespace idhan::js
{

using ConvertError = std::string;

template < typename T >
std::expected< T, ConvertError > convertTo( JSContext* ctx, JSValueConst value );

template <>
inline std::expected< std::string, ConvertError > convertTo( JSContext* ctx, JSValueConst value )
{
	std::size_t length {};
	const char* text { JS_ToCStringLen( ctx, &length, value ) };

	if ( text == nullptr )
	{
		return std::unexpected { "Failed to convert string." };
	}

	std::string result { text, length };

	JS_FreeCString( ctx, text );

	return result;
}

template < typename T >
JSValue convertFrom( JSContext* ctx, const T&& value );

template <>
inline JSValue convertFrom< std::string >( JSContext* ctx, const std::string&& value )
{
	return JS_NewStringLen( ctx, value.data(), value.size() );
}

template < typename T >
inline JSValue convertOptional( JSContext* ctx, const std::optional< T >&& value )
{
	return value ? convertFrom< T >( ctx, std::move( value.value() ) ) : JS_NULL;
}

} // namespace idhan::js