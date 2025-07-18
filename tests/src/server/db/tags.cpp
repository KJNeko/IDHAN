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

namespace
{
constexpr idhan::TagDomainID DEFAULT_DOMAIN { 1 };

// Test fixture class to encapsulate common setup
class TagTestFixture
{
  public:

	TagTestFixture() : c { "host=localhost dbname=idhan-db user=idhan" }, w { c }
	{
		// Set search path to test schema
		w.exec( "SET search_path='test'" );

		// Create dummy record for testing
		std::string str {};
		for ( std::size_t i = 0; i < ( 256 / 8 ); ++i ) str += "F0";

		const auto dummy_record {
			w.exec( std::format( "INSERT INTO records (sha256) VALUES (\'\\x{}\'::bytea) RETURNING record_id", str ) )
		};
		dummy_id = dummy_record[ 0 ][ 0 ].as< idhan::RecordID >();
	}

	idhan::TagID createTag( const std::string& namespace_name, const std::string& subtag )
	{
		return w.exec_params( "SELECT createTag($1, $2)", namespace_name, subtag )[ 0 ][ 0 ].as< idhan::TagID >();
	}

	void createAlias( const idhan::TagID aliased_id, const idhan::TagID alias_id )
	{
		w.exec_params(
			"INSERT INTO tag_aliases (domain_id, aliased_id, alias_id) VALUES ($1, $2, $3)",
			DEFAULT_DOMAIN,
			aliased_id,
			alias_id );
		w.commit();
	}

	void deleteAlias( const idhan::TagID aliased_id, const idhan::TagID alias_id )
	{
		w.exec_params(
			"DELETE FROM tag_aliases WHERE domain_id = $1 AND aliased_id = $2 AND alias_id = $3",
			DEFAULT_DOMAIN,
			aliased_id,
			alias_id );
		w.commit();
	}

	void createParent( const idhan::TagID parent_id, const idhan::TagID child_id )
	{
		w.exec_params(
			"INSERT INTO tag_parents (domain_id, parent_id, child_id) VALUES ($1, $2, $3)",
			DEFAULT_DOMAIN,
			parent_id,
			child_id );
		w.commit();
	}

	void deleteParent( const idhan::TagID parent_id, const idhan::TagID child_id )
	{
		w.exec_params(
			"DELETE FROM tag_parents WHERE domain_id = $1 AND parent_id = $2 AND child_id = $3",
			DEFAULT_DOMAIN,
			parent_id,
			child_id );
		w.commit();
	}

	void createMapping( const idhan::TagID id )
	{
		w.exec_params(
			"INSERT INTO tag_mappings (domain_id, record_id, tag_id) VALUES ($1, $2, $3)",
			DEFAULT_DOMAIN,
			dummy_id,
			id );
		w.commit();
	}

	void deleteMapping( const idhan::TagID id )
	{
		w.exec_params( "DELETE FROM tag_mappings WHERE tag_id = $1 AND domain_id = $2", id, DEFAULT_DOMAIN );
		w.commit();
	}

	void verifyFlatAlias( const idhan::TagID aliased_id, const idhan::TagID alias_id )
	{
		const auto result { w.exec_params(
			"SELECT aliased_id, alias_id FROM flattened_aliases WHERE aliased_id = $1 AND alias_id = $2",
			aliased_id,
			alias_id ) };
		w.commit();

		CAPTURE( aliased_id );
		CAPTURE( alias_id );
		REQUIRE( result.size() == 1 );
	}

	void verifyFlatAliasChain(
		const idhan::TagID aliased_id, const idhan::TagID alias_id, const idhan::TagID original_alias )
	{
		const auto result { w.exec_params(
			"SELECT aliased_id, alias_id, original_alias_id FROM flattened_aliases WHERE aliased_id = $1 AND alias_id = $2",
			aliased_id,
			alias_id ) };
		w.commit();

		CAPTURE( aliased_id );
		CAPTURE( alias_id );
		CAPTURE( original_alias );

		REQUIRE( result.size() == 1 );
		REQUIRE( result[ 0 ][ "aliased_id" ].as< idhan::TagID >() == aliased_id );
		REQUIRE( result[ 0 ][ "alias_id" ].as< idhan::TagID >() == alias_id );
		REQUIRE( result[ 0 ][ "original_alias_id" ].as< idhan::TagID >() == original_alias );
	}

	void verifyMapping( const idhan::TagID tag_id )
	{
		const auto result { w.exec_params(
			"SELECT tag_id, ideal_tag_id FROM tag_mappings WHERE domain_id = $1 AND tag_id = $2",
			DEFAULT_DOMAIN,
			tag_id ) };
		REQUIRE( result.size() == 1 );

		const auto returned_tag_id = result[ 0 ][ "tag_id" ].as< idhan::TagID >();
		CAPTURE( returned_tag_id );

		const auto ideal_tag_null = result[ 0 ][ "ideal_tag_id" ].is_null();
		REQUIRE( ideal_tag_null );
	}

	void verifyMappingIdealised( const idhan::TagID tag_id, const idhan::TagID ideal_tag_id )
	{
		const auto result { w.exec_params(
			"SELECT tag_id, ideal_tag_id FROM tag_mappings WHERE domain_id = $1 AND tag_id = $2 AND ideal_tag_id IS NOT NULL",
			DEFAULT_DOMAIN,
			tag_id ) };
		REQUIRE( result.size() == 1 );
		REQUIRE( result[ 0 ][ "ideal_tag_id" ].as< idhan::TagID >() == ideal_tag_id );
	}

	void verifyParentMapping( const idhan::TagID tag_id, const idhan::TagID origin_id )
	{
		const auto result { w.exec_params(
			"SELECT origin_id, tag_id FROM tag_mappings_virtuals WHERE domain_id = $1 AND tag_id = $2 AND origin_id = $3",
			DEFAULT_DOMAIN,
			tag_id,
			origin_id ) };
		REQUIRE( result.size() == 1 );
	}

	pqxx::connection c;
	pqxx::work w;
	idhan::RecordID dummy_id;
};
} // namespace

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
		fixture.verifyFlatAlias( tag_empty_toujou, tag_character_toujou );
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

		fixture.verifyFlatAlias( tag_character_toujou, tag_character_shrione );
		fixture.verifyFlatAliasChain( tag_empty_toujou, tag_character_shrione, tag_character_toujou );
	}

	SECTION( "Alias chain deletion and restoration" )
	{
		fixture.createAlias( tag_empty_toujou, tag_character_toujou );
		fixture.createAlias( tag_character_toujou, tag_character_shrione );

		// Delete middle alias
		fixture.deleteAlias( tag_character_toujou, tag_character_shrione );
		fixture.verifyFlatAlias( tag_empty_toujou, tag_character_toujou );

		// Recreate middle alias
		fixture.createAlias( tag_character_toujou, tag_character_shrione );
		fixture.verifyFlatAliasChain( tag_empty_toujou, tag_character_shrione, tag_character_toujou );
		fixture.verifyFlatAlias( tag_character_toujou, tag_character_shrione );
	}

	SECTION( "Circular alias chain prevention" )
	{
		const auto A = fixture.createTag( "", "A" );
		const auto B = fixture.createTag( "", "B" );
		const auto C = fixture.createTag( "", "C" );

		// A -> B -> C
		fixture.createAlias( A, B );
		fixture.createAlias( B, C );

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
	fixture.createAlias( tag_4, tag_5 );
	fixture.createAlias( tag_5, tag_6 );

	fixture.verifyFlatAliasChain( tag_1, tag_3, tag_2 );
	fixture.verifyFlatAliasChain( tag_4, tag_6, tag_5 );

	// Connect the chains
	fixture.createAlias( tag_3, tag_4 );
	fixture.verifyFlatAliasChain( tag_1, tag_6, tag_2 );
	fixture.verifyFlatAliasChain( tag_4, tag_6, tag_5 );
}

TEST_CASE( "Tag parent relationships", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	TagTestFixture fixture;

	const auto tag_toujou = fixture.createTag( "", "toujou koneko" );
	const auto tag_highschool_dxd = fixture.createTag( "", "highschool dxd" );

	SECTION( "Parent relationship with mapping" )
	{
		fixture.createParent( tag_highschool_dxd, tag_toujou );
		fixture.createMapping( tag_toujou );

		fixture.verifyParentMapping( tag_highschool_dxd, tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
	}

	SECTION( "Parent relationship with aliases" )
	{
		const auto tag_character_toujou = fixture.createTag( "character", "toujou koneko" );
		const auto tag_series_highschool_dxd = fixture.createTag( "series", "highschool dxd" );

		fixture.createParent( tag_highschool_dxd, tag_toujou );
		fixture.createMapping( tag_toujou );

		// Add alias to child
		fixture.createAlias( tag_toujou, tag_character_toujou );
		fixture.verifyParentMapping( tag_highschool_dxd, tag_character_toujou );

		// Remove child alias
		fixture.deleteAlias( tag_toujou, tag_character_toujou );
		fixture.verifyParentMapping( tag_highschool_dxd, tag_toujou );

		// Add alias to parent
		fixture.createAlias( tag_highschool_dxd, tag_series_highschool_dxd );
		fixture.verifyParentMapping( tag_series_highschool_dxd, tag_toujou );

		// Remove parent alias
		fixture.deleteAlias( tag_highschool_dxd, tag_series_highschool_dxd );
		fixture.verifyParentMapping( tag_highschool_dxd, tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
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
		fixture.verifyMapping( tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
	}

	SECTION( "Mapping with single alias idealization" )
	{
		fixture.createMapping( tag_toujou );
		fixture.createAlias( tag_toujou, tag_character_toujou );

		fixture.verifyMappingIdealised( tag_toujou, tag_character_toujou );

		// Remove alias
		fixture.deleteAlias( tag_toujou, tag_character_toujou );
		fixture.verifyMapping( tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
	}

	SECTION( "Mapping with chained alias idealization" )
	{
		fixture.createMapping( tag_toujou );
		fixture.createAlias( tag_toujou, tag_character_toujou );
		fixture.createAlias( tag_character_toujou, tag_character_shrione );

		fixture.verifyMappingIdealised( tag_toujou, tag_character_shrione );

		// Remove first alias in chain
		fixture.deleteAlias( tag_toujou, tag_character_toujou );
		fixture.verifyMapping( tag_toujou );

		// Cleanup
		fixture.deleteMapping( tag_toujou );
	}
}
