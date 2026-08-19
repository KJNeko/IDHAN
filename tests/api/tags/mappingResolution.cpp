#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

//! A record only gets active mappings once it holds a file, which no tagging endpoint gives it. The mime
//! matters as well as the row: search drops anything whose mime was never resolved.
static void attachFile( pqxx::connection& connection, const RecordID record_id )
{
	pqxx::nontransaction tx { connection };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_delete_time) "
		"VALUES ($1, 0, (SELECT mime_id FROM mime WHERE name = 'image/png'), now())",
		pqxx::params { record_id } );
}

//! What the resolution layer settled on for the record, which is what a search reads.
static std::vector< TagID > finalTags( pqxx::connection& connection, const RecordID record_id, const TagDomainID tag_domain_id )
{
	pqxx::nontransaction tx { connection };

	std::vector< TagID > tags {};

	for ( const auto& row : tx.exec(
			  "SELECT DISTINCT tag_id FROM active_tag_mappings_final WHERE record_id = $1 AND tag_domain_id = $2 "
			  "ORDER BY tag_id",
			  pqxx::params { record_id, tag_domain_id } ) )
		tags.emplace_back( row[ 0 ].as< TagID >() );

	return tags;
}

static std::vector< TagID > sorted( std::vector< TagID > tags )
{
	std::sort( tags.begin(), tags.end() );
	return tags;
}

SCENARIO_METHOD( ServerFixture, "Relationships reaching record mappings", "[api][tags][mappings]" )
{
	GIVEN( "a domain, a record holding a file, and two tags" )
	{
		const auto domain { api().createDomain( "characters" ) };
		const auto record { api().createRecord( 1 ) };
		attachFile( db(), record );

		const auto samus { api().createTag( "character", "samus" ) };
		const auto samus_aran { api().createTag( "character", "samus aran" ) };
		const auto metroid { api().createTag( "series", "metroid" ) };

		WHEN( "a tag with no relationships is added" )
		{
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == drogon::k200OK );

			THEN( "a search for it finds the record" )
			{
				CHECK( api().search( domain, { samus } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == std::vector { samus } );
			}

			AND_WHEN( "the tag is removed again" )
			{
				REQUIRE( api().removeTags( domain, { record }, { samus } ).status == drogon::k200OK );

				THEN( "the search no longer finds it" )
				{
					CHECK( api().search( domain, { samus } ).empty() );
					CHECK( finalTags( db(), record, domain ).empty() );
				}
			}
		}

		WHEN( "a parent is declared and the child is then added" )
		{
			REQUIRE( api().createParents( domain, { { metroid, samus } } ).status == drogon::k200OK );
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == drogon::k200OK );

			THEN( "a search for the parent finds the record" )
			{
				CHECK( api().search( domain, { metroid } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == sorted( { samus, metroid } ) );
			}
		}

		WHEN( "a tag is added and a parent is declared over it afterwards" )
		{
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == drogon::k200OK );
			REQUIRE( api().createParents( domain, { { metroid, samus } } ).status == drogon::k200OK );

			THEN( "a search for the parent finds the record" )
			{
				CHECK( api().search( domain, { metroid } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == sorted( { samus, metroid } ) );
			}
		}

		WHEN( "an alias is created and the aliased tag is then added" )
		{
			REQUIRE( api().createAliases( domain, { { samus, samus_aran } } ).status == drogon::k200OK );
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == drogon::k200OK );

			THEN( "a search for the alias target finds the record" )
			{
				CHECK( api().search( domain, { samus_aran } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == std::vector { samus_aran } );
			}

			THEN( "a search for the tag that was aliased away finds nothing" )
			{
				CHECK( api().search( domain, { samus } ).empty() );
			}
		}

		WHEN( "a tag is added and aliased onward afterwards" )
		{
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == drogon::k200OK );
			REQUIRE( api().createAliases( domain, { { samus, samus_aran } } ).status == drogon::k200OK );

			THEN( "a search for the alias target finds the record" )
			{
				CHECK( api().search( domain, { samus_aran } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == std::vector { samus_aran } );
			}
		}
	}
}

} // namespace idhan::test
