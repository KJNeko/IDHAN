//
// Created by kj16609 on 8/2/26.
//
#pragma once

#include <string>

#ifdef _WIN32
#define FGL_EXPORT __declspec( dllexport )
#else
#define FGL_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

//! The handful of declarations every other module ABI header needs.
/** This header exists to break a cycle rather than to group anything conceptually: ModuleBase needs
 *  ModuleFile (a call carries one) and ModuleFile needs ModuleError (it returns one), so the pieces
 *  both of them depend on cannot live in either. Nothing here should grow beyond that role -- if a
 *  declaration is only needed by one side, it belongs on that side. */
namespace idhan
{

//! Human-readable error message returned (via std::expected) when a module operation fails.
using ModuleError = std::string;

} // namespace idhan
