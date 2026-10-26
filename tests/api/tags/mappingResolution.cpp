#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

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
static std::vector< TagID > finalTags(
	pqxx::connection& connection,
	const RecordID record_id,
	const TagDomainID tag_domain_id )
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
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == 200 );

			THEN( "a search for it finds the record" )
			{
				CHECK( api().search( domain, { samus } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == std::vector { samus } );
			}

			AND_WHEN( "the tag is removed again" )
			{
				REQUIRE( api().removeTags( domain, { record }, { samus } ).status == 200 );

				THEN( "the search no longer finds it" )
				{
					CHECK( api().search( domain, { samus } ).empty() );
					CHECK( finalTags( db(), record, domain ).empty() );
				}
			}
		}

		WHEN( "a parent is declared and the child is then added" )
		{
			REQUIRE( api().createParents( domain, { { metroid, samus } } ).status == 200 );
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == 200 );

			THEN( "a search for the parent finds the record" )
			{
				CHECK( api().search( domain, { metroid } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == sorted( { samus, metroid } ) );
			}
		}

		WHEN( "a tag is added and a parent is declared over it afterwards" )
		{
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == 200 );
			REQUIRE( api().createParents( domain, { { metroid, samus } } ).status == 200 );

			THEN( "a search for the parent finds the record" )
			{
				CHECK( api().search( domain, { metroid } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == sorted( { samus, metroid } ) );
			}
		}

		WHEN( "an alias is created and the aliased tag is then added" )
		{
			REQUIRE( api().createAliases( domain, { { samus, samus_aran } } ).status == 200 );
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == 200 );

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
			REQUIRE( api().addTags( domain, { record }, { samus } ).status == 200 );
			REQUIRE( api().createAliases( domain, { { samus, samus_aran } } ).status == 200 );

			THEN( "a search for the alias target finds the record" )
			{
				CHECK( api().search( domain, { samus_aran } ) == std::vector { record } );
				CHECK( finalTags( db(), record, domain ) == std::vector { samus_aran } );
			}
		}
	}
}

TEST_CASE_METHOD(
	ServerFixture,
	"Queued counts survive restart while alias search stays current",
	"[api][tags][counts][queued]" )
{
	const auto domain { api().createDomain( "queued counts" ) };
	const auto source { api().createTag( "", "source" ) };
	const auto target { api().createTag( "", "target" ) };
	const auto record { api().createRecord( 1 ) };
	attachFile( db(), record );
	REQUIRE( api().createAliases( domain, { { source, target } } ).status == 200 );
	{
		pqxx::work pause_consumer { db() };
		pause_consumer.exec( "SELECT pg_advisory_xact_lock('tag_count_delta'::regclass::oid::bigint)" );
		REQUIRE( api().addTags( domain, { record }, { source } ).status == 200 );
		CHECK( api().search( domain, { target } ) == std::vector { record } );
		CHECK( pause_consumer.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
		CHECK( pause_consumer.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 2 );
		pause_consumer.commit();
	}
	// Startup runs the real maintenance job immediately, including work left by a crashed server.
	testServer().restart( true );
	bool drained {};
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
	while ( !drained && std::chrono::steady_clock::now() < deadline )
	{
		pqxx::nontransaction read { db() };
		drained = read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 0;
		if ( !drained ) std::this_thread::sleep_for( std::chrono::milliseconds { 10 } );
	}
	REQUIRE( drained );
	CHECK( api().search( domain, { target } ) == std::vector { record } );
	const auto info { api().get( "/tags/" + std::to_string( target ) + "/info" ) };
	REQUIRE( info.status == 200 );
	CHECK( info.json[ "display_count" ].asInt64() == 1 );
}

} // namespace idhan::test
