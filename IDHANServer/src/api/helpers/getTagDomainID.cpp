//
// Created by kj16609 on 3/11/25.
//

#include "createBadRequest.hpp"
#include "helpers.hpp"

namespace idhan::api::helpers
{

std::expected< TagDomainID, drogon::HttpResponsePtr > getTagDomainIDParameter( const drogon::HttpRequestPtr& request )
{
	try
	{
		const auto tag_domain_parameter = request->getOptionalParameter< std::string >( "tag_domain_id" );

		if ( !tag_domain_parameter )
			return std::unexpected( createBadRequest( "tag_domain_id not supplied in query" ) );

		const std::string tag_domain_str { *tag_domain_parameter };

		if ( tag_domain_str.empty() )
		{
			return std::unexpected( createBadRequest( "Tag domain was an empty string upon parsing" ) );
		}

		const std::uint64_t tag_domain_id { std::stoull( tag_domain_str ) };

		if ( tag_domain_id > std::numeric_limits< TagDomainID >::max() )
			return std::unexpected( createBadRequest( "Invalid tag_domain_id: Domain ID out of range of type" ) );

		if ( !( tag_domain_id > 0 ) )
			return std::unexpected( createBadRequest( "Invalid tag_domain_id: Domain ID must be greater than zero" ) );

		return static_cast< TagDomainID >( tag_domain_id );
	}
	catch ( ... )
	{
		return std::unexpected( createBadRequest( "Invalid tag_domain_id" ) );
	}
}

} // namespace idhan::api::helpers
