#include "DatabaseSecrets.hpp"


#include <vector>
#include <sstream>
#include <unordered_set>
#include <json/json.h>

#include "IDHANDownloader/secrets/SecretDomain.hpp"

#include "DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan::downloader
{

std::expected< Json::Value, std::string > decodeDomainSecrets( const std::string& json )
{
	Json::Value value;
	Json::CharReaderBuilder reader;
	std::istringstream input { json };
	std::string errors;

	if ( !Json::parseFromStream( reader, input, &value, &errors ) )
		return std::unexpected( "Invalid domain secrets JSON" );

	if ( !value.isObject() ) return std::unexpected( "Domain secrets must be a JSON object" );

	return value;
}

std::expected< Json::Value, std::string > DatabaseSecrets::secrets( const std::string_view domain )
{
	const auto domain_e { secretDomain( domain ) };
	if ( !domain_e.has_value() ) return std::unexpected( domain_e.error() );

	const auto snapshot { m_cache.load() };
	if ( snapshot == nullptr ) return std::unexpected( "Downloader secrets have not been loaded" );

	const auto found { snapshot->documents.find( domain_e.value() ) };
	if ( found == snapshot->documents.end() ) return Json::Value { Json::objectValue };

	return found->second;
}

runtime::Task< std::expected< void, std::string > > DatabaseSecrets::reload()
{
	try
	{
		auto lock { co_await m_update_mutex.co_scoped_lock() };
		const auto rows {
			co_await DownloadSessionManager::dbContext()
					 .query(
						 //language=postgresql
						 R"SQL(
						SELECT
						    domain,
						    secrets::text
						FROM downloader_domain_secrets
					)SQL" )
					 .rows< std::string, std::string >()
		};
		auto next { std::make_shared< Snapshot >() };
		for ( const auto& [ domain, json ] : rows )
		{
			const auto domain_e { secretDomain( domain ) };
			if ( !domain_e.has_value() ) co_return std::unexpected( domain_e.error() );

			const auto document_e { decodeDomainSecrets( json ) };
			if ( !document_e.has_value() ) co_return std::unexpected( document_e.error() );

			if ( !next->documents.emplace( domain_e.value(), document_e.value() ).second )
				co_return std::unexpected( "Duplicate normalized secret domain" );

			next->serialized.emplace( domain_e.value(), json );
		}
		m_cache.store( std::move( next ) );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( format_ns::format( "Unable to reload downloader secrets: {}", e.what() ) );
	}
	co_return std::expected< void, std::string > {};
}

runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > DatabaseSecrets::values()
	const
{
	const auto snapshot { m_cache.load() };
	if ( snapshot == nullptr ) co_return std::unexpected( "Downloader secrets have not been loaded" );

	co_return snapshot->serialized;
}

runtime::Task< std::expected< void, std::string > > DatabaseSecrets::set(
	std::unordered_map< std::string, std::string > values )
{
	try
	{
		auto lock { co_await m_update_mutex.co_scoped_lock() };
		const auto current { m_cache.load() };
		if ( current == nullptr ) co_return std::unexpected( "Downloader secrets have not been loaded" );

		auto next { std::make_shared< Snapshot >( *current ) };
		std::vector< std::string > names {};
		std::vector< std::string > secrets {};
		names.reserve( values.size() );
		secrets.reserve( values.size() );

		std::unordered_set< std::string > domains;

		for ( auto& [ name, value ] : values )
		{
			const auto domain_e { secretDomain( name ) };

			if ( !domain_e.has_value() ) co_return std::unexpected( domain_e.error() );

			if ( !domains.emplace( domain_e.value() ).second )
				co_return std::unexpected( "Duplicate normalized secret domain" );

			const auto object_e { decodeDomainSecrets( value ) };

			if ( !object_e.has_value() ) co_return std::unexpected( object_e.error() );

			next->documents[ domain_e.value() ] = object_e.value();
			next->serialized[ domain_e.value() ] = value;
			names.emplace_back( domain_e.value() );
			secrets.emplace_back( std::move( value ) );
		}

		db::DBBindings bindings {};
		bindings.bind( names );
		bindings.bind( db::secret( std::move( secrets ) ) );
		co_await DownloadSessionManager::dbContext().query(
			//language=postgresql
			R"SQL(
				INSERT INTO downloader_domain_secrets (domain, secrets)
				SELECT
				    domain,
				    document::jsonb
				FROM UNNEST($1::text[], $2::text[]) AS input (domain,
				        document)
				ON CONFLICT (domain)
				    DO UPDATE SET
				        secrets = EXCLUDED.secrets
			)SQL",
			std::move( bindings ) );
		// The DB statement is atomic. Publish only after it succeeds, without a second read.
		m_cache.store( std::move( next ) );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( format_ns::format( "Unable to store downloader secrets: {}", e.what() ) );
	}

	co_return std::expected< void, std::string > {};
}

} // namespace idhan::downloader
