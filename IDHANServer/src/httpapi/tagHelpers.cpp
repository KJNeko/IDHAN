#include "tagHelpers.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>

namespace idhan::httpapi
{

template < typename T >
[[nodiscard]] static http::PathParameter integerPathParameter( std::string name )
{
	auto validate = [ name ]( const std::string_view value ) -> std::expected< void, http::ParseError >
	{
		T id {};
		const auto result { std::from_chars( value.data(), value.data() + value.size(), id ) };
		if ( result.ec != std::errc {} || result.ptr != value.data() + value.size() || id <= 0 )
			return std::unexpected( http::ParseError { name, "must be a positive integer" } );
		return {};
	};
	return http::PathParameter { std::move( name ), std::move( validate ) };
}

http::PathParameter tagIdPathParameter( std::string name )
{
	return integerPathParameter< TagID >( std::move( name ) );
}

http::PathParameter tagDomainIdPathParameter( std::string name )
{
	return integerPathParameter< TagDomainID >( std::move( name ) );
}

runtime::Task< std::optional< TagDomainInfo > > findTagDomain( const TagDomainID id, db::DBContext& db )
{
	db::DBBindings bindings {};
	bindings.bind( id );

	const auto rows {
		co_await db
			.query(
				"SELECT tag_domain_id, domain_name FROM tag_domains WHERE tag_domain_id = $1", std::move( bindings ) )
			.rows< TagDomainID, std::string >()
	};

	if ( rows.empty() ) co_return std::nullopt;

	const auto& [ domain_id, domain_name ] { rows.row( 0 ) };
	co_return TagDomainInfo { .id = domain_id, .name = domain_name };
}

runtime::Task< std::optional< TagDomainInfo > > findTagDomain( const std::string_view name, db::DBContext& db )
{
	db::DBBindings bindings {};
	bindings.bind( std::string { name } );

	const auto rows {
		co_await db
			.query( "SELECT tag_domain_id, domain_name FROM tag_domains WHERE domain_name = $1", std::move( bindings ) )
			.rows< TagDomainID, std::string >()
	};

	if ( rows.empty() ) co_return std::nullopt;

	const auto& [ domain_id, domain_name ] { rows.row( 0 ) };
	co_return TagDomainInfo { .id = domain_id, .name = domain_name };
}

runtime::Task< std::vector< TagDomainInfo > > listTagDomains( db::DBContext& db )
{
	const auto rows { co_await db.query( "SELECT tag_domain_id, domain_name FROM tag_domains ORDER BY tag_domain_id" )
		                  .rows< TagDomainID, std::string >() };

	std::vector< TagDomainInfo > domains {};
	domains.reserve( rows.size() );

	for ( const auto& [ domain_id, domain_name ] : rows ) domains.push_back( TagDomainInfo { domain_id, domain_name } );

	co_return domains;
}

std::expected< TagDomainID, http::Response > tagDomainIdParameter( const http::RequestParameters& parameters )
{
	const auto value_e { parameters.scalar< std::int64_t >( "tag_domain_id" ) };
	if ( !value_e ) return std::unexpected( http::Response::error( 400, "tag_domain_id not supplied in query" ) );

	if ( *value_e <= 0 || *value_e > std::numeric_limits< TagDomainID >::max() )
		return std::unexpected( http::Response::error( 400, "Invalid tag_domain_id: out of range" ) );

	return static_cast< TagDomainID >( *value_e );
}

runtime::Task< std::expected< void, http::Response > > validateRelationshipIds(
	const TagDomainID tag_domain_id,
	std::vector< TagID > tag_ids,
	db::DBContext& db )
{
	const auto domain { co_await findTagDomain( tag_domain_id, db ) };
	if ( !domain )
		co_return std::unexpected(
			http::Response::error( 404, std::format( "Tag domain {} does not exist", tag_domain_id ) ) );

	std::ranges::sort( tag_ids );
	const auto duplicates { std::ranges::unique( tag_ids ) };
	tag_ids.erase( duplicates.begin(), duplicates.end() );

	db::DBBindings bindings {};
	bindings.bind( tag_ids );

	const auto rows {
		co_await db.query( "SELECT tag_id FROM tags WHERE tag_id = ANY($1)", std::move( bindings ) ).rows< TagID >()
	};

	if ( rows.size() == tag_ids.size() ) co_return {};

	std::vector< TagID > found_ids {};
	found_ids.reserve( rows.size() );
	for ( const auto& [ tag_id ] : rows ) found_ids.push_back( tag_id );
	std::ranges::sort( found_ids );

	for ( const auto tag_id : tag_ids )
		if ( !std::ranges::binary_search( found_ids, tag_id ) )
			co_return std::unexpected( http::Response::error( 404, std::format( "Tag {} does not exist", tag_id ) ) );

	co_return {};
}

runtime::Task< std::expected< std::vector< TagID >, http::Response > > createTagsFromPairs(
	std::vector< std::pair< std::string, std::string > > tag_pairs,
	db::DBContext& db )
{
	if ( tag_pairs.empty() ) co_return std::unexpected( http::Response::error( 400, "No tags to create" ) );

	std::vector< std::string > namespaces {};
	std::vector< std::string > subtags {};
	namespaces.reserve( tag_pairs.size() );
	subtags.reserve( tag_pairs.size() );
	for ( const auto& [ tag_namespace, subtag ] : tag_pairs )
	{
		namespaces.push_back( tag_namespace );
		subtags.push_back( subtag );
	}

	db::DBBindings bindings {};
	bindings.bind( namespaces );
	bindings.bind( subtags );

	try
	{
		const auto rows {
			co_await db.query( "SELECT tag_id FROM createBatchTags($1, $2)", std::move( bindings ) ).rows< TagID >()
		};

		std::vector< TagID > tag_ids {};
		tag_ids.reserve( rows.size() );
		for ( const auto& [ tag_id ] : rows )
		{
			if ( tag_id <= 0 )
				co_return std::unexpected(
					http::Response::error(
						500, std::format( "Failed to create tag, got {}. Expected tag_id > 0", tag_id ) ) );
			tag_ids.push_back( tag_id );
		}

		if ( tag_ids.size() != tag_pairs.size() )
			co_return std::unexpected(
				http::Response::error(
					500,
					std::format(
						"Failed to create tags. Count mismatch: expected {} got {}",
						tag_pairs.size(),
						tag_ids.size() ) ) );

		co_return tag_ids;
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( http::Response::error( 500, std::format( "Failed to create tags: {}", e.what() ) ) );
	}
}

runtime::Task< std::optional< NamespaceID > > findNamespace( std::string name, db::DBContext& db )
{
	db::DBBindings bindings {};
	bindings.bind( name );

	const auto rows {
		co_await db.query( "SELECT namespace_id FROM tag_namespaces WHERE namespace_text = $1", std::move( bindings ) )
			.rows< NamespaceID >()
	};

	if ( rows.empty() ) co_return std::nullopt;

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::expected< NamespaceID, http::Response > > createNamespace( std::string name, db::DBContext& db )
{
	if ( const auto existing { co_await findNamespace( name, db ) }; existing ) co_return *existing;

	db::DBBindings bindings {};
	bindings.bind( name );

	const auto rows {
		co_await db
			.query(
				"INSERT INTO tag_namespaces (namespace_text) VALUES ($1) RETURNING namespace_id",
				std::move( bindings ) )
			.rows< NamespaceID >()
	};

	if ( rows.empty() )
		co_return std::unexpected( http::Response::error( 500, std::format( "Failed to create namespace {}", name ) ) );

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::optional< TagID > > findTag(
	const NamespaceID namespace_id,
	std::string subtag_text,
	db::DBContext& db )
{
	db::DBBindings bindings {};
	bindings.bind( namespace_id );
	bindings.bind( subtag_text );

	const auto rows {
		co_await db
			.query(
				"SELECT tag_id FROM tags WHERE namespace_id = $1 "
				"AND subtag_text = NORMALIZE(CASEFOLD(NORMALIZE($2, NFC)), NFC)",
				std::move( bindings ) )
			.rows< TagID >()
	};

	if ( rows.empty() ) co_return std::nullopt;

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::expected< TagID, http::Response > > createTag(
	const NamespaceID namespace_id,
	std::string subtag_text,
	db::DBContext& db )
{
	if ( const auto existing { co_await findTag( namespace_id, subtag_text, db ) }; existing ) co_return *existing;

	db::DBBindings bindings {};
	bindings.bind( namespace_id );
	bindings.bind( subtag_text );

	const auto rows {
		co_await db
			.query(
				"INSERT INTO tags (namespace_id, subtag_text) VALUES ($1, $2) "
				"ON CONFLICT (namespace_id, subtag_text) DO UPDATE SET subtag_text = EXCLUDED.subtag_text "
				"RETURNING tag_id",
				std::move( bindings ) )
			.rows< TagID >()
	};

	if ( rows.empty() )
		co_return std::unexpected(
			http::Response::error( 500, std::format( "Failed to create tag: {}:{}", namespace_id, subtag_text ) ) );

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::expected< TagID, http::Response > > createTag(
	std::string tag_namespace,
	std::string subtag_text,
	db::DBContext& db )
{
	auto namespace_id { co_await createNamespace( std::move( tag_namespace ), db ) };
	if ( !namespace_id ) co_return std::unexpected( std::move( namespace_id.error() ) );

	co_return co_await createTag( *namespace_id, std::move( subtag_text ), db );
}

} // namespace idhan::httpapi
