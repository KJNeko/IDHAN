#include "tags.hpp"

namespace idhan
{

drogon::Task< std::optional< TagDomainInfo > > findTagDomain( const TagDomainID id, DbClientPtr db )
{
	const auto result {
		co_await db->execSqlCoro( "SELECT tag_domain_id, domain_name FROM tag_domains WHERE tag_domain_id = $1", id )
	};

	if ( result.empty() ) co_return std::nullopt;

	co_return TagDomainInfo { .id = result[ 0 ][ "tag_domain_id" ].as< TagDomainID >(),
		                      .name = result[ 0 ][ "domain_name" ].as< std::string >() };
}

drogon::Task< std::optional< TagDomainInfo > > findTagDomain( const std::string_view name, DbClientPtr db )
{
	const auto result {
		co_await db->execSqlCoro( "SELECT tag_domain_id, domain_name FROM tag_domains WHERE domain_name = $1", name )
	};

	if ( result.empty() ) co_return std::nullopt;

	co_return TagDomainInfo { .id = result[ 0 ][ "tag_domain_id" ].as< TagDomainID >(),
		                      .name = result[ 0 ][ "domain_name" ].as< std::string >() };
}

drogon::Task< std::vector< TagDomainInfo > > listTagDomains( DbClientPtr db )
{
	const auto rows {
		co_await db->execSqlCoro( "SELECT tag_domain_id, domain_name FROM tag_domains ORDER BY tag_domain_id" )
	};

	std::vector< TagDomainInfo > domains {};
	domains.reserve( rows.size() );

	for ( const auto& row : rows )
	{
		domains.emplace_back( row[ "tag_domain_id" ].as< TagDomainID >(), row[ "domain_name" ].as< std::string >() );
	}

	co_return domains;
}

} // namespace idhan
