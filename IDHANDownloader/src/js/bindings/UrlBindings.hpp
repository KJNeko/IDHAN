#pragma once

#include <quickjs.h>

namespace idhan::downloader::quickjs
{
bool installURLBindings( JSContext* context, JSValueConst global );
}
