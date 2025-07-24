//
// Created by kj16609 on 7/15/25.
//

#include <pqxx/pqxx>

#include <iostream>
#include <thread>

#include "IDHANTypes.hpp"
#include "catch2/catch_test_macros.hpp"
#include "serverStarterHelper.hpp"
#include "spdlog/spdlog.h"

constexpr idhan::TagDomainID DEFAULT_DOMAIN { 1 };

// Test fixture class to encapsulate common setup
struct TagTestFixture
{
	pqxx::connection c;
	idhan::RecordID dummy_id;
	pqxx::work w;

	TagTestFixture() :
	  c { "host=localhost dbname=idhan-db user=idhan" },
	  w { c }
	{
		spdlog::set_level( spdlog::level::debug );
		// c.set_verbosity( pqxx::error_verbosity::verbose );
		c.set_notice_handler(
			[]( const pqxx::zview& message )
			{
				// if message starts with DEBUG then print it
				if ( message.starts_with( "DEBUG" ) )
				{
					spdlog::debug( message );
				}
				else
				{
					spdlog::info( message );
				}
			} );

		// Set search path to test schema
		w.exec( "SET search_path='test'" );

		// Allow for debugs to print from sql
		w.exec( "SET client_min_messages = 'debug1';" );

		// Create dummy record for testing
		std::string str {};
		for ( std::size_t i = 0; i < ( 256 / 8 ); ++i ) str += "F0";

		const auto dummy_record {
			w.exec( std::format( "INSERT INTO records (sha256) VALUES (\'\\x{}\'::bytea) RETURNING record_id", str ) )
		};
		dummy_id = dummy_record[ 0 ][ 0 ].as< idhan::RecordID >();
	}

	// Example refactored functions showing the pattern:

	void tryCommit()
	{
		// w.commit();
	}

	idhan::TagID createTag( const std::string& namespace_name, const std::string& subtag )
	{
		auto query = R"( SELECT * FROM createTag($1, $2);)";

		CAPTURE( query );
		const auto result { w.exec( query, pqxx::params { namespace_name, subtag } ) };
		tryCommit();
		return result.one_row()[ 0 ].as< idhan::TagID >();
	}

	void createMapping( const idhan::TagID tag_id )
	{
		const auto query { "INSERT INTO tag_mappings (domain_id, record_id, tag_id) VALUES ($1, $2, $3)" };
		pqxx::params params { DEFAULT_DOMAIN, dummy_id, tag_id };
		w.exec( query, params );
		tryCommit();
	}

	void deleteMapping( const idhan::TagID tag_id )
	{
		const auto query { "DELETE FROM tag_mappings WHERE domain_id = $1 AND record_id = $2 AND tag_id = $3" };
		pqxx::params params { DEFAULT_DOMAIN, dummy_id, tag_id };
		w.exec( query, params );
		tryCommit();
	}

	void createAlias( const idhan::TagID aliased_id, const idhan::TagID tag_id )
	{
		const auto query { "INSERT INTO tag_aliases (domain_id, aliased_id, alias_id) VALUES ($1, $2, $3)" };
		pqxx::params params { DEFAULT_DOMAIN, aliased_id, tag_id };
		w.exec( query, params );
		tryCommit();
	}

	void deleteAlias( const idhan::TagID aliased_id, const idhan::TagID tag_id )
	{
		const auto query { "DELETE FROM tag_aliases WHERE domain_id = $1 AND aliased_id = $2 AND alias_id = $3" };
		pqxx::params params { DEFAULT_DOMAIN, aliased_id, tag_id };
		w.exec( query, params );
		tryCommit();
	}

	void createParent( const idhan::TagID parent_id, const idhan::TagID child_id )
	{
		const auto query { "INSERT INTO tag_parents (domain_id, parent_id, child_id) VALUES ($1, $2, $3)" };
		pqxx::params params { DEFAULT_DOMAIN, parent_id, child_id };
		w.exec( query, params );
		tryCommit();
	}

	void deleteParent( const idhan::TagID parent_id, const idhan::TagID child_id )
	{
		const auto query { "DELETE FROM tag_parents WHERE domain_id = $1 AND parent_id = $2 AND child_id = $3" };
		pqxx::params params { DEFAULT_DOMAIN, parent_id, child_id };
		w.exec( query, params );
		tryCommit();
	}
};

#define REQUIRE_VIRTUAL_TAG( origin_id_i, tag_id_i )                                           \
	{                                                                                          \
		const auto virtual_mappings { fixture.getVirtualMappings() };                          \
		REQUIRE(                                                                               \
			std::ranges::find_if(                                                              \
				virtual_mappings,                                                              \
				[]( const VirtualTagMapping& mapping ) -> bool                                 \
				{ return mapping.tag_id = tag_id_i && mapping.origin_id == origin_id_i; } ) ); \
	}

#define REQUIRE_MAPPING_IDEAL( tag_id, ideal_id )                                                                       \
	{                                                                                                                   \
		const pqxx::params params { fixture.dummy_id, tag_id, DEFAULT_DOMAIN, ideal_id };                               \
		const auto result { fixture.w.exec(                                                                             \
			"SELECT 1 FROM tag_mappings WHERE record_id = $1 AND tag_id = $2 AND domain_id = $3 AND ideal_tag_id = $4", \
			params ) };                                                                                                 \
		REQUIRE( result.size() == 1 );                                                                                  \
	}

#define REQUIRE_MAPPING( tag_id )                                                                                     \
	{                                                                                                                 \
		const pqxx::params params { fixture.dummy_id, tag_id, DEFAULT_DOMAIN };                                       \
		const auto result {                                                                                           \
			fixture.w                                                                                                 \
				.exec( "SELECT 1 FROM tag_mappings WHERE record_id = $1 AND tag_id = $2 AND domain_id = $3", params ) \
		};                                                                                                            \
		REQUIRE( result.size() == 1 );                                                                                \
	}

#define REQUIRE_FLATTENED_ALIAS( aliased_id, alias_id )                                                         \
	{                                                                                                           \
		const pqxx::params params { aliased_id, alias_id };                                                     \
		const auto result {                                                                                     \
			fixture.w.exec( "SELECT 1 FROM flattened_aliases WHERE aliased_id = $1 AND alias_id = $2", params ) \
		};                                                                                                      \
		REQUIRE( result.size() == 1 );                                                                          \
	}

#define REQUIRE_PARENT_MAPPING( origin_id, parent_id )                                                                 \
	{                                                                                                                  \
		const pqxx::params params { fixture.dummy_id, parent_id, origin_id };                                          \
		const auto result { fixture.w.exec(                                                                           \
			"SELECT 1 FROM tag_mappings_virtual WHERE record_id = $1 AND tag_id = $2 AND origin_id = $3", params ) }; \
		REQUIRE( result.size() == 1 );                                                                                 \
	}

TEST_CASE( "Tag table existence and basic creation", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	// Test that `tags` table exists
	const auto result { fixture.w.exec( "SELECT * FROM tags" ) };
	REQUIRE( result.size() == 0 );

	// Test tag creation
	const auto tag_result { fixture.w.exec( "SELECT createTag('character', 'toujou koneko')" ) };
	REQUIRE( tag_result.size() == 1 );
	REQUIRE( tag_result[ 0 ][ 0 ].as< idhan::TagID >() == 1 );
}

TEST_CASE( "Tag alias self-reference protection", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	const auto tag_self_ref { fixture.createTag( "", "self_ref" ) };
	REQUIRE_THROWS( fixture.createAlias( tag_self_ref, tag_self_ref ) );
}

TEST_CASE( "Basic tag aliases", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	const auto tag_empty_toujou = fixture.createTag( "", "toujou koneko" );
	const auto tag_character_toujou = fixture.createTag( "character", "toujou koneko" );

	SECTION( "Creating simple alias" )
	{
		fixture.createAlias( tag_empty_toujou, tag_character_toujou );
		REQUIRE_FLATTENED_ALIAS( tag_empty_toujou, tag_character_toujou );
	}

	SECTION( "Recursive alias prevention" )
	{
		const auto tag_character_shrione = fixture.createTag( "character", "shrione" );

		fixture.createAlias( tag_character_toujou, tag_character_shrione );
		REQUIRE_THROWS( fixture.createAlias( tag_character_shrione, tag_character_toujou ) );
	}
}

TEST_CASE( "Tag alias chains", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	const auto tag_empty_toujou = fixture.createTag( "", "toujou koneko" );
	const auto tag_character_toujou = fixture.createTag( "character", "toujou koneko" );
	const auto tag_character_shrione = fixture.createTag( "character", "shrione" );

	SECTION( "Two-level alias chain" )
	{
		fixture.createAlias( tag_empty_toujou, tag_character_toujou );
		fixture.createAlias( tag_character_toujou, tag_character_shrione );

		REQUIRE_FLATTENED_ALIAS( tag_empty_toujou, tag_character_shrione );
		REQUIRE_FLATTENED_ALIAS( tag_character_toujou, tag_character_shrione );
	}

	SECTION( "Alias chain deletion and restoration" )
	{
		fixture.createAlias( tag_empty_toujou, tag_character_toujou );
		fixture.createAlias( tag_character_toujou, tag_character_shrione );

		// Delete middle alias
		fixture.deleteAlias( tag_character_toujou, tag_character_shrione );
		REQUIRE_FLATTENED_ALIAS( tag_empty_toujou, tag_character_toujou );

		// Recreate middle alias
		fixture.createAlias( tag_character_toujou, tag_character_shrione );
		REQUIRE_FLATTENED_ALIAS( tag_empty_toujou, tag_character_shrione );
		REQUIRE_FLATTENED_ALIAS( tag_character_toujou, tag_character_shrione );
	}

	SECTION( "Circular alias chain prevention" )
	{
		const auto A = fixture.createTag( "", "A" );
		const auto B = fixture.createTag( "", "B" );
		const auto C = fixture.createTag( "", "C" );

		// A -> B -> C
		fixture.createAlias( A, B );
		fixture.createAlias( B, C );

		THEN( "Circular references must throw" )
		{
			// Attempt to create circular references
			// C -> A
			REQUIRE_THROWS( fixture.createAlias( C, A ) );
			// C -> B
			REQUIRE_THROWS( fixture.createAlias( C, B ) );

			// B -> A
			REQUIRE_THROWS( fixture.createAlias( B, A ) );

			REQUIRE_THROWS( fixture.createAlias( A, A ) );
			REQUIRE_THROWS( fixture.createAlias( B, B ) );
			REQUIRE_THROWS( fixture.createAlias( C, C ) );
		}

		SECTION( "New chains are prevented if circular" )
		{
			// D -> E
			const auto D = fixture.createTag( "", "D" );
			const auto E = fixture.createTag( "", "E" );

			fixture.createAlias( D, E );
			fixture.createAlias( E, A );

			REQUIRE_THROWS( fixture.createAlias( C, D ) );
		}
	}
}

TEST_CASE( "Complex alias chain repair", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	const auto tag_1 = fixture.createTag( "", "tag_1" );
	const auto tag_2 = fixture.createTag( "", "tag_2" );
	const auto tag_3 = fixture.createTag( "", "tag_3" );
	const auto tag_4 = fixture.createTag( "", "tag_4" );
	const auto tag_5 = fixture.createTag( "", "tag_5" );
	const auto tag_6 = fixture.createTag( "", "tag_6" );

	// Create two separate chains
	fixture.createAlias( tag_1, tag_2 );
	fixture.createAlias( tag_2, tag_3 );

	REQUIRE_FLATTENED_ALIAS( tag_1, tag_3 );
	REQUIRE_FLATTENED_ALIAS( tag_2, tag_3 );

	fixture.createAlias( tag_4, tag_5 );
	fixture.createAlias( tag_5, tag_6 );
	REQUIRE_FLATTENED_ALIAS( tag_4, tag_6 );
	REQUIRE_FLATTENED_ALIAS( tag_5, tag_6 );

	// Connect the chains
	fixture.createAlias( tag_3, tag_4 );
	REQUIRE_FLATTENED_ALIAS( tag_1, tag_6 );
	REQUIRE_FLATTENED_ALIAS( tag_2, tag_6 );
	REQUIRE_FLATTENED_ALIAS( tag_3, tag_6 );
	REQUIRE_FLATTENED_ALIAS( tag_4, tag_6 );
	REQUIRE_FLATTENED_ALIAS( tag_5, tag_6 );
}

TEST_CASE( "Tag parent relationships", "[tags][db][server][parents]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	const auto tag_ahri { fixture.createTag( "character", "ahri (league of legends)" ) };
	const auto tag_league { fixture.createTag( "series", "league of legends" ) };
	const auto tag_riot { fixture.createTag( "copyright", "riot games" ) };

	GIVEN( "A record with mapping `character:ahri (league of legends)" )
	{
		fixture.createMapping( tag_ahri );

		THEN( "The mapping should exist" )
		{
			REQUIRE_MAPPING( tag_ahri );
		}

		WHEN( "Adding a parent of 'series:league of legends' to 'character:ahri(league of legends)'" )
		{
			fixture.createParent( tag_league, tag_ahri );

			THEN( "The mapping should exist in the aliased_parents table" )
			{
				const auto result { fixture.w.exec( "SELECT * FROM aliased_parents" ) };
				REQUIRE( result.size() == 1 );

				const auto result_row { result[ 0 ] };
				REQUIRE( result_row[ "original_parent_id" ].as< idhan::TagID >() == tag_league );
				REQUIRE( result_row[ "original_child_id" ].as< idhan::TagID >() == tag_ahri );

				REQUIRE( result_row[ "parent_id" ].is_null() );
				REQUIRE( result_row[ "child_id" ].is_null() );
			}

			THEN( "The virtual mappings table should have a tag of `series:league of legends` for record 1" )
			{
				const auto result { fixture.w.exec(
					"SELECT COUNT(*) FROM tag_mappings_virtual WHERE record_id = $1 AND tag_id = $2 AND origin_id = $3",
					pqxx::params { fixture.dummy_id, tag_league, tag_ahri } ) };
				REQUIRE( result[ 0 ][ 0 ].as< int >() == 1 );
			}

			AND_WHEN( "Adding a parent of `copyright:riot games` to `series:league of legends`" )
			{
				fixture.createParent( tag_riot, tag_league );

				THEN( "The aliased_parents table should have two rows" )
				{
					const auto result { fixture.w.exec( "SELECT * FROM aliased_parents" ) };
					REQUIRE( result.size() == 2 );
				}

				THEN( "The virtual mappings table should have a tag of `series:league of legends`" )
				{
					const auto result1 { fixture.w.exec(
						"SELECT COUNT(*) FROM tag_mappings_virtual WHERE record_id = $1 AND tag_id = $2 AND origin_id = $3",
						pqxx::params { fixture.dummy_id, tag_league, tag_ahri } ) };
					REQUIRE( result1[ 0 ][ 0 ].as< int >() == 1 );
				}

				THEN( "The virtual mappings table should have a tag of `copyright:riot games`" )
				{
					const auto result2 { fixture.w.exec(
						"SELECT COUNT(*) FROM tag_mappings_virtual WHERE record_id = $1 AND tag_id = $2 AND origin_id = $3",
						pqxx::params { fixture.dummy_id, tag_riot, tag_league } ) };
					REQUIRE( result2[ 0 ][ 0 ].as< int >() == 1 );
				}

				AND_WHEN( "The parent `series:league of legends` is removed from `character:ahri (league of legends)" )
				{
					fixture.deleteParent( tag_league, tag_ahri );

					THEN( "The only row that should remain should be league -> riot" )
					{
						const auto result { fixture.w.exec( "SELECT * FROM aliased_parents" ) };
						REQUIRE( result.size() == 1 );

						const auto result_row { result[ 0 ] };
						REQUIRE( result_row[ "original_parent_id" ].as< idhan::TagID >() == tag_riot );
						REQUIRE( result_row[ "original_child_id" ].as< idhan::TagID >() == tag_league );
					}

					AND_WHEN( "The mapping is re-added" )
					{
						fixture.createParent( tag_league, tag_ahri );

						THEN( "The aliased parents should have two rows" )
						{
							const auto result { fixture.w.exec( "SELECT * FROM aliased_parents" ) };
							REQUIRE( result.size() == 2 );
						}

						THEN( "The virtual mappings should be restored" )
						{
							const auto result { fixture.w.exec( "SELECT * FROM tag_mappings_virtual" ) };

							// Helps with debugging
							CHECKED_IF( result.size() == 1 )
							{
								const auto result_row { result[ 0 ] };

								// Check that the only row is at least the first part of the chain (tag_ahri -> tag_league)
								REQUIRE( result_row[ "record_id" ].as< idhan::RecordID >() == fixture.dummy_id );
								REQUIRE( result_row[ "tag_id" ].as< idhan::TagID >() == tag_league );
								REQUIRE( result_row[ "origin_id" ].as< idhan::TagID >() == tag_ahri );
								REQUIRE( result_row[ "domain_id" ].as< idhan::TagDomainID >() == DEFAULT_DOMAIN );
							}

							REQUIRE( result.size() == 2 );
						}
					}
				}
			}

			AND_WHEN( "The parent relationship is deleted" )
			{
				fixture.deleteParent( tag_league, tag_ahri );

				THEN( "The virtual table should have no mappings" )
				{
					const auto total_result { fixture.w.exec( "SELECT COUNT(*) FROM tag_mappings_virtual" ) };
					REQUIRE( total_result[ 0 ][ 0 ].as< int >() == 0 );
				}
			}
		}
	}
}

TEST_CASE( "Tag mappings and idealization", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	const auto tag_toujou = fixture.createTag( "", "toujou koneko" );
	const auto tag_character_toujou = fixture.createTag( "character", "toujou koneko" );
	const auto tag_character_shrione = fixture.createTag( "character", "shrione" );

	SECTION( "Basic mapping without idealization" )
	{
		fixture.createMapping( tag_toujou );
		REQUIRE_MAPPING( tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
	}

	SECTION( "Mapping with single alias idealization" )
	{
		fixture.createMapping( tag_toujou );
		fixture.createAlias( tag_toujou, tag_character_toujou );

		REQUIRE_MAPPING_IDEAL( tag_toujou, tag_character_toujou );
		// fixture.verifyMappingIdealised( tag_toujou, tag_character_toujou );

		// Remove alias
		fixture.deleteAlias( tag_toujou, tag_character_toujou );
		REQUIRE_MAPPING( tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
	}

	SECTION( "Mapping with chained alias idealization" )
	{
		fixture.createMapping( tag_toujou );
		fixture.createAlias( tag_toujou, tag_character_toujou );
		fixture.createAlias( tag_character_toujou, tag_character_shrione );

		REQUIRE_MAPPING_IDEAL( tag_toujou, tag_character_shrione );

		// Remove first alias in chain
		fixture.deleteAlias( tag_toujou, tag_character_toujou );
		REQUIRE_MAPPING( tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
	}
}
