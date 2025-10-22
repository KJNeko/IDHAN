//
// Created by kj16609 on 10/21/25.
//
#pragma once
#include "fgl/defines.hpp"
#include "mime/MimeMatchBase.hpp"

namespace Json
{
class Value;
}

namespace idhan::mime
{
class MimeMatchInclude : public MimeMatchBase
{
	std::vector< MimeMatcher > m_matchers {};

  public:

	FGL_DELETE_ALL_RO5( MimeMatchInclude );

	MimeMatchInclude( std::vector< MimeMatcher >&& matchers, const Json::Value& json );
	drogon::Task< bool > match( Cursor& cursor ) const override;

	static MimeMatcher createFromJson( const Json::Value& json );
};
} // namespace idhan::mime