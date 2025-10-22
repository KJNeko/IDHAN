//
// Created by kj16609 on 10/21/25.
//
#pragma once
#include <fgl/defines.hpp>

#include "drogon/utils/coroutine.h"

namespace Json
{
class Value;
}

namespace idhan::mime
{
struct MimeMatchBase;
class Cursor;
using MimeMatcher = std::unique_ptr< MimeMatchBase >;

std::vector< MimeMatcher > parseDataJson( const Json::Value& json );

struct MimeMatchBase
{
	//! If true then this must match
	bool m_required;

	std::vector< MimeMatcher > m_children {};

	FGL_DELETE_ALL_RO5( MimeMatchBase );
	MimeMatchBase( const Json::Value& json );
	virtual ~MimeMatchBase() = default;

	drogon::Task< bool > test( Cursor cursor );
	virtual drogon::Task< bool > match( Cursor& cursor ) const = 0;
};

} // namespace idhan::mime