#pragma once
#include <memory>

class IDHANJSRuntime;

namespace idhan::js
{
std::shared_ptr< IDHANJSRuntime > installBindings( std::shared_ptr< IDHANJSRuntime > ctx );
}