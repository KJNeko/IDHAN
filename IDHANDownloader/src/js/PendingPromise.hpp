#pragma once

#include <quickjs.h>

namespace idhan::downloader
{

struct PendingPromise
{
	JSContext* context {};
	JSValue resolve { JS_UNDEFINED };
	JSValue reject { JS_UNDEFINED };

	void resolveOrReject( bool ok, JSValue value );
	void discard();
};

} // namespace idhan::downloader
