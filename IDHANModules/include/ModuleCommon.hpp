#pragma once

#include <string>

#ifdef _WIN32
#define FGL_EXPORT __declspec( dllexport )
#else
#define FGL_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

//! The handful of declarations every other module ABI header needs.
namespace idhan
{

//! Human-readable error message returned (via std::expected) when a module operation fails.
using ModuleError = std::string;

} // namespace idhan
