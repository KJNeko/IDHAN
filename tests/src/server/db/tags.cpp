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

#define CREATE_TESTTAG( NAMESPACE, SUBTAG )                                                                            \
	[ & ]() -> idhan::TagID                                                                                            \
	{ return w.exec_params( "SELECT createTag($1, $2)", NAMESPACE, SUBTAG )[ 0 ][ 0 ].as< idhan::TagID >(); }()

TEST_CASE( "Tag Tests", "[tags][db][server]" )
{
	SERVER_HANDLE;
	std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

	constexpr idhan::TagDomainID DEFAULT_DOMAIN { 1 };

	pqxx::connection c { "host=localhost dbname=idhan-db user=idhan" };
	pqxx::work w { c };

	// set search path to test to swap to the test schema
	w.exec( "SET search_path='test'" );

	std::string str {};
	for ( std::size_t i = 0; i < ( 256 / 8 ); ++i ) str += "F0";

	const auto dummy_record {
		w.exec( std::format( "INSERT INTO records (sha256) VALUES (\'\\x{}\'::bytea) RETURNING record_id", str ) )
	};
	const auto dummy_id { dummy_record[ 0 ][ 0 ].as< idhan::RecordID >() };

	auto createAlias = [ & ]( const idhan::TagID aliased_id, const idhan::TagID alias_id )
	{
		w.exec_params(
			"INSERT INTO tag_aliases (domain_id, aliased_id, alias_id) VALUES ($1, $2, $3)",
			DEFAULT_DOMAIN,
			aliased_id,
			alias_id );
		w.commit();
	};

	auto deleteAlias = [ & ]( const idhan::TagID aliased_id, const idhan::TagID alias_id )
	{
		w.exec_params(
			"DELETE FROM tag_aliases WHERE domain_id = $1 AND aliased_id = $2 AND alias_id = $3",
			DEFAULT_DOMAIN,
			aliased_id,
			alias_id );
		w.commit();
	};

	auto createParent = [ & ]( const idhan::TagID parent_id, const idhan::TagID child_id )
	{
		w.exec_params(
			"INSERT INTO tag_parents (domain_id, parent_id, child_id) VALUES ($1, $2, $3)",
			DEFAULT_DOMAIN,
			parent_id,
			child_id );
		w.commit();
	};

	auto deleteParent = [ & ]( const idhan::TagID parent_id, const idhan::TagID child_id )
	{
		w.exec_params(
			"DELETE FROM tag_parents WHERE domain_id = $1 AND parent_id = $2 AND child_id = $3",
			DEFAULT_DOMAIN,
			parent_id,
			child_id );
		w.commit();
	};

	auto createMapping = [ & ]( const idhan::TagID id )
	{
		w.exec_params(
			"INSERT INTO tag_mappings (domain_id, record_id, tag_id) VALUES ($1, $2, $3)",
			DEFAULT_DOMAIN,
			dummy_id,
			id );
		w.commit();
	};

	auto deleteMapping = [ & ]( const idhan::TagID id )
	{
		w.exec_params( "DELETE FROM tag_mappings WHERE tag_id = $1 AND domain_id = $2", id, DEFAULT_DOMAIN );
		w.commit();
	};

	auto testFlatAlias = [ & ]( const idhan::TagID aliased_id, const idhan::TagID alias_id )
	{
		const auto result { w.exec_params(
			"SELECT aliased_id, alias_id FROM flattened_aliases WHERE aliased_id = $1 AND alias_id = $2",
			aliased_id,
			alias_id ) };
		w.commit();

		CAPTURE( aliased_id );
		CAPTURE( alias_id );

		REQUIRE( result.size() == 1 );
	};

	auto testFlatAliasChain =
		[ & ]( const idhan::TagID aliased_id, const idhan::TagID alias_id, const idhan::TagID original_alias )
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
	};

	auto testMapping = [ & ]( const idhan::TagID tag_id )
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
	};

	auto testMappingIdealised = [ & ]( const idhan::TagID tag_id, const idhan::TagID ideal_tag_id )
	{
		const auto result { w.exec_params(
			"SELECT tag_id, ideal_tag_id FROM tag_mappings WHERE domain_id = $1 AND tag_id = $2 AND ideal_tag_id IS NOT NULL",
			DEFAULT_DOMAIN,
			tag_id ) };
		REQUIRE( result.size() == 1 );
		REQUIRE( result[ 0 ][ "ideal_tag_id" ].as< idhan::TagID >() == ideal_tag_id );
	};

	auto testParentMapping = [ & ]( const idhan::TagID tag_id, const idhan::TagID origin_id )
	{
		const auto result { w.exec_params(
			"SELECT origin_id, tag_id FROM tag_mappings_virtuals WHERE domain_id = $1 AND tag_id = $2 AND origin_id = $3",
			DEFAULT_DOMAIN,
			tag_id,
			origin_id ) };
		REQUIRE( result.size() == 1 );
	};

	SECTION( "Table check" )
	{
		// test that `tags` table exists
		const auto result { w.exec( "SELECT * FROM tags" ) };

		REQUIRE( result.size() == 0 );

		SECTION( "Tag creation" )
		{
			const auto result { w.exec( "SELECT createTag('character', 'toujou koneko')" ) };
			REQUIRE( result.size() == 1 );
			REQUIRE( result[ 0 ][ 0 ].as< idhan::TagID >() == 1 );
		}

		const auto tag_e_toujou_koneko { CREATE_TESTTAG( "", "toujou koneko" ) };
		const auto tag_character_toujou_koneko { CREATE_TESTTAG( "character", "toujou koneko" ) };
		const auto tag_character_shrione { CREATE_TESTTAG( "character", "shrione" ) };
		const auto tag_series_highschool_dxd { CREATE_TESTTAG( "series", "highschool dxd" ) };
		const auto tag_highschool_dxd { CREATE_TESTTAG( "", "highschool dxd" ) };
		const auto tag_white_hair { CREATE_TESTTAG( "", "white hair" ) };

		SECTION( "Aliases" )
		{
			WHEN( "Creating alias `toujou koneko` -> `character:toujou koneko`" )
			{
				createAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );
				testFlatAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );

				AND_WHEN( "Adding alias `character:toujou koneko` -> `character:shrione`" )
				{
					createAlias( tag_character_toujou_koneko, tag_character_shrione );
					testFlatAlias( tag_character_toujou_koneko, tag_character_shrione );
					THEN( "The alias `toujou koneko` -> `character:shrione` should now exist" )
					{
						testFlatAliasChain( tag_e_toujou_koneko, tag_character_shrione, tag_character_toujou_koneko );
					}

					AND_WHEN( "Alias `character:toujou koneko` -> `character:shrione` is deleted" )
					{
						// Test that deletes are propogated properly
						deleteAlias( tag_character_toujou_koneko, tag_character_shrione );
						THEN( "Alias `toujou koneko` -> `character:shrione` should no longer exist" )
						{
							testFlatAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );
						}

						AND_WHEN( "Alias `character:toujou koneko` -> `character:shrione` is recreated" )
						{
							createAlias( tag_character_toujou_koneko, tag_character_shrione );
							THEN( "Previous aliases should be restored" )
							{
								testFlatAliasChain(
									tag_e_toujou_koneko, tag_character_shrione, tag_character_toujou_koneko );
								testFlatAlias( tag_character_toujou_koneko, tag_character_shrione );
							}
						}
					}
				}
			}

			WHEN( "A recursive chain is created" )
			{
				createAlias( tag_character_toujou_koneko, tag_character_shrione );
				THEN( "The chain creation process should throw" )
				{
					REQUIRE_THROWS( createAlias( tag_character_shrione, tag_character_toujou_koneko ) );
				}
			}
		}

		SECTION( "Alias chains" )
		{
			const auto tag_1 { CREATE_TESTTAG( "", "tag_1" ) };
			const auto tag_2 { CREATE_TESTTAG( "", "tag_2" ) };
			const auto tag_3 { CREATE_TESTTAG( "", "tag_3" ) };
			const auto tag_4 { CREATE_TESTTAG( "", "tag_4" ) };
			const auto tag_5 { CREATE_TESTTAG( "", "tag_5" ) };
			const auto tag_6 { CREATE_TESTTAG( "", "tag_6" ) };

			createAlias( tag_1, tag_2 );
			createAlias( tag_2, tag_3 );

			// createAlias(tag_3, tag_4);

			createAlias( tag_4, tag_5 );
			createAlias( tag_5, tag_6 );

			testFlatAliasChain( tag_1, tag_3, tag_2 );
			testFlatAliasChain( tag_4, tag_6, tag_5 );

			THEN( "Chains should be repaired" )
			{
				createAlias( tag_3, tag_4 );
				testFlatAliasChain( tag_1, tag_6, tag_2 );
				testFlatAliasChain( tag_4, tag_6, tag_5 );
			}
		}

		SECTION( "Parents" )
		{
			WHEN( "Parent `highschool dxd` is added to `toujou koneko`" )
			{
				createParent( tag_highschool_dxd, tag_e_toujou_koneko );
				AND_WHEN( "`toujou koneko` is added as a mapping" )
				{
					createMapping( tag_e_toujou_koneko );

					THEN( "The virtual mappings should have the tag `highschool dxd` with the origin `toujou koneko`" )
					{
						testParentMapping( tag_highschool_dxd, tag_e_toujou_koneko );
					}

					AND_WHEN( "Parent `highschool dxd` is deleted" )
					{
						deleteMapping( tag_e_toujou_koneko );

						THEN( "The mapping should no longer exist" )
						{
							testParentMapping( tag_highschool_dxd, tag_e_toujou_koneko );
						}
					}

					AND_WHEN( "Alias `toujou koneko` -> `character:toujou koneko` is added" )
					{
						createAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );
						THEN(
							"The virtual mapping should be `highschool dxd` with an origin of `character:toujou koneko`" )
						{
							testParentMapping( tag_highschool_dxd, tag_character_toujou_koneko );
						}

						AND_WHEN( "The previous alias is deleted" )
						{
							deleteAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );

							testParentMapping( tag_highschool_dxd, tag_e_toujou_koneko );
						}
					}

					AND_WHEN( "Alias `highschool dxd` -> `series:highschool dxd` is added" )
					{
						createAlias( tag_highschool_dxd, tag_series_highschool_dxd );
						THEN(
							"The virtual mapping should be `series:highschool dxd` with an origin of `toujou koneko`" )
						{
							testParentMapping( tag_series_highschool_dxd, tag_e_toujou_koneko );
						}

						AND_WHEN( "The previous alias is deleted" )
						{
							deleteAlias( tag_highschool_dxd, tag_series_highschool_dxd );

							testParentMapping( tag_highschool_dxd, tag_e_toujou_koneko );
						}
					}

					// Cleanup for the next tests
					deleteMapping( tag_e_toujou_koneko );
				}
			}

			createMapping( tag_e_toujou_koneko );
			THEN( "The mapping should exist" )
			{
				testMapping( tag_e_toujou_koneko );
			}

			WHEN( "Alias `toujou koneko` -> `character:toujou koneko` is created" )
			{
				createAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );
				THEN( "Mapping should become `character:shrione` as it's ideal" )
				{
					testMappingIdealised( tag_e_toujou_koneko, tag_character_toujou_koneko );

					AND_WHEN( "Alias `toujou koneko` -> `character:toujou koneko` is deleted" )
					{
						deleteAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );

						THEN( "Mapping should have no ideal" )
						{
							testMapping( tag_e_toujou_koneko );
						}
					}

					AND_WHEN( "Another alias is added" )
					{
						createAlias( tag_character_toujou_koneko, tag_character_shrione );

						THEN( "Mapping should have updated to `character:shrione`" )
						{
							testMappingIdealised( tag_e_toujou_koneko, tag_character_shrione );
						}

						AND_WHEN( "Tag `toujou koneko` has it's alias removed" )
						{
							deleteAlias( tag_e_toujou_koneko, tag_character_toujou_koneko );

							THEN( "Mapping should have updated to `toujou koneko`" )
							{
								testMapping( tag_e_toujou_koneko );
							}
						}
					}
				}
			}

			WHEN( "Parent `highschool dxd` is added to `toujou koneko`" )
			{
				createParent( tag_highschool_dxd, tag_e_toujou_koneko );
				THEN( "highschool dxd should appear as a mapping" )
				{}
			}
		}
	}

	SECTION( "Alias self reference" )
	{
		const auto tag_self_ref { CREATE_TESTTAG( "", "self_ref" ) };
		REQUIRE_THROWS( createAlias( tag_self_ref, tag_self_ref ) );
	}

	SUCCEED();
}
