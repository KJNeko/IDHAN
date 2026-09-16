#include "AssociateTags.hpp"

#include <format>
#include <utility>

#include "httpapi/tagHelpers.hpp"
#include "idhan/db/DBContext.hpp"
#include "splitTag.hpp"

namespace idhan::helpers
{

runtime::Task< std::expected< void, std::string > > associateTags(
	const RecordID record_id,
	std::vector< std::string > tags,
	std::string tag_domain,
	db::DBContext& db )
{
	if ( tags.empty() ) co_return std::expected< void, std::string > {};

	try
	{
		const auto domain { co_await httpapi::findTagDomain( tag_domain, db ) };
		if ( !domain ) co_return std::unexpected( std::format( "Tag domain '{}' does not exist", tag_domain ) );

		std::vector< std::string > namespaces {};
		std::vector< std::string > subtags {};
		namespaces.reserve( tags.size() );
		subtags.reserve( tags.size() );

		for ( const auto& tag : tags )
		{
			auto [ tag_namespace, tag_subtag ] { splitTag( tag ) };
			namespaces.emplace_back( std::move( tag_namespace ) );
			subtags.emplace_back( std::move( tag_subtag ) );
		}

		db::DBBindings create_bindings {};
		create_bindings.bind( namespaces );
		create_bindings.bind( subtags );
		const auto created {
			co_await db
				.query( "SELECT tag_id FROM createbatchtags($1::TEXT[], $2::TEXT[])", std::move( create_bindings ) )
				.rows< TagID >()
		};

		std::vector< TagID > tag_ids {};
		tag_ids.reserve( created.size() );
		for ( const auto& [ tag_id ] : created ) tag_ids.emplace_back( tag_id );

		db::DBBindings mapping_bindings {};
		mapping_bindings.bind( record_id );
		mapping_bindings.bind( tag_ids );
		mapping_bindings.bind( domain->id );
		co_await db
			.query(
				"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
				"SELECT $1, unnest($2::" TAG_PG_TYPE_NAME "[]), $3 ON CONFLICT DO NOTHING",
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
