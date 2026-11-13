#pragma once

#include <string>

#ifdef _WIN32
#define FGL_EXPORT __declspec( dllexport )
#else
#define FGL_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

namespace idhan
{

using ModuleError = std::string;

} // namespace idhan
