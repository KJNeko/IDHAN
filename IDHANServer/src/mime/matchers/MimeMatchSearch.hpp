//
// Created by kj16609 on 10/21/25.
//
#pragma once
#include "../MimeMatchBase.hpp"

namespace Json
{
class Value;
}

namespace idhan::mime
{

class MimeMatchSearch final : public MimeMatchBase
{
	std::vector< std::vector< std::byte > > m_match_data {};

	static constexpr auto NO_OFFSET { std::numeric_limits< std::int64_t >::max() };
	static constexpr auto NO_LIMIT { std::numeric_limits< std::size_t >::max() };
	std::int64_t m_offset { NO_OFFSET };
	std::size_t m_limit { std::numeric_limits< std::size_t >::max() };

	drogon::Task< bool > match( Cursor& cursor ) const override;

  public:

	MimeMatchSearch( const Json::Value& json );

	static MimeMatcher createFromJson( const Json::Value& json );

	~MimeMatchSearch() = default;
};

} // namespace idhan::mime
