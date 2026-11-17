#pragma once

#include <memory>
#include <string>

#include "IDHANJSRuntime.hpp"
#include "fgl/defines.hpp"

struct JSReturn
{
	std::shared_ptr< IDHANJSRuntime > m_ctx { nullptr };
	JSValue m_value { JS_UNDEFINED };

	FGL_DELETE_COPY( JSReturn );

	[[nodiscard]] bool isException() const;
	[[nodiscard]] bool isUndefined() const;
	[[nodiscard]] bool isNull() const;
	[[nodiscard]] bool isObject() const;
	[[nodiscard]] bool isString() const;
	[[nodiscard]] bool isArray() const;
	[[nodiscard]] bool isFunction() const;
	[[nodiscard]] JSReturn stringifyJson() const;
	[[nodiscard]] std::string toString() const;

	// Missing properties remain JS_UNDEFINED; access exceptions throw.
	[[nodiscard]] JSReturn getProperty( const char* name ) const;

	JSReturn( JSReturn&& other ) noexcept;
	JSReturn& operator=( JSReturn&& other ) noexcept;

	JSReturn() = default;

	explicit JSReturn( std::shared_ptr< IDHANJSRuntime > ctx );
	explicit JSReturn( std::shared_ptr< IDHANJSRuntime > ctx, JSValue value );

	~JSReturn();
};
