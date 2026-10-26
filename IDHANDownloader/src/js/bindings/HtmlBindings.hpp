#pragma once

#include <quickjs.h>

namespace idhan::downloader::quickjs
{
bool installHTMLBindings( JSContext* context );

JSValue parseHTML( JSContext* context, JSValueConst this_value, int argument_count, JSValueConst* arguments );
} // namespace idhan::downloader::quickjs
