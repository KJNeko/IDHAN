#include "AssociateUrls.hpp"

#include <utility>

#include "UrlDomain.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::helpers
{

runtime::Task< std::expected< void, std::string > > associateUrls(
	const RecordID record_id,
	std::vector< std::string > urls,
	db::DBContext& db )
{
	if ( urls.empty() ) co_return std::expected< void, std::string > {};

	std::vector< std::string > domains {};
	domains.reserve( urls.size() );
	for ( const auto& url : urls ) domains.emplace_back( extractDomain( url ) );

	try
	{
		db::DBBindings domain_bindings {};
		domain_bindings.bind( domains );
		co_await db
			.query(
				"INSERT INTO url_domains (url_domain) SELECT DISTINCT unnest($1::text[]) "
				"ON CONFLICT (url_domain) DO NOTHING",
				std::move( domain_bindings ) )
			.result();

		db::DBBindings url_bindings {};
		url_bindings.bind( urls );
		url_bindings.bind( domains );
		co_await db
			.query(
				"INSERT INTO urls (url, url_domain_id) "
				"SELECT pairs.url, ud.url_domain_id "
				"FROM unnest($1::text[], $2::text[]) AS pairs(url, domain) "
				"JOIN url_domains ud ON ud.url_domain = pairs.domain "
				"ON CONFLICT (url) DO NOTHING",
				std::move( url_bindings ) )
			.result();

		db::DBBindings mapping_bindings {};
		mapping_bindings.bind( urls );
		mapping_bindings.bind( record_id );
		co_await db
			.query(
				"INSERT INTO url_mappings (url_id, record_id) "
				"SELECT url_id, $2 FROM urls WHERE url = ANY($1::text[]) ON CONFLICT DO NOTHING",
				std::move( mapping_bindings ) )
			.result();
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}

	co_return std::expected< void, std::string > {};
}

} // namespace idhan::helpers
