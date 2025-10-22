//
// Created by kj16609 on 10/21/25.
//
#pragma once
#include <fgl/defines.hpp>

#include "drogon/utils/coroutine.h"
#include "threading/ImmedientTask.hpp"

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
  protected:

	std::vector< MimeMatcher > m_children {};

  public:

	FGL_DELETE_ALL_RO5( MimeMatchBase );
	MimeMatchBase( const Json::Value& json );
	virtual ~MimeMatchBase() = default;

	coro::ImmedientTask< bool > test( Cursor cursor );
	virtual coro::ImmedientTask< bool > match( Cursor& cursor ) const = 0;
};

} // namespace idhan::mime