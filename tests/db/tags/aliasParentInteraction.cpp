#include <catch2/catch_test_macros.hpp>

#include "MappingHelpers.hpp"
#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

SCENARIO_METHOD( MigratedSchema, "Aliases and parents resolving together", "[db][tags][aliases][parents][mappings]" )
{
	pqxx::work tx { connection() };

	GIVEN( "a mapped tag whose parent is aliased afterwards" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus aran" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };
		const auto metroid_series { insertTag( tx, "series", "metroid (series)" ) };

		insertParent( tx, domain, metroid, samus );
		insertMapping( tx, record, samus, domain );

		REQUIRE( parentRows( tx, record, domain ) == std::vector { ParentRow { metroid, samus, 0 } } );

		WHEN( "the parent is aliased onto another tag" )
		{
			insertAlias( tx, domain, metroid, metroid_series );

			THEN( "the parent the record was given is rewritten to the alias target" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus, metroid_series } ) );
				CHECK( parentRows( tx, record, domain ) == std::vector { ParentRow { metroid_series, samus, 0 } } );
			}

			THEN( "the parent row itself names the alias target as its ideal" )
			{
				CHECK(
					tx.query_value< int >(
						"SELECT COALESCE(ideal_parent_id, parent_id) FROM tag_parents "
						"WHERE parent_id = $1 AND child_id = $2 AND tag_domain_id = $3",
						pqxx::params { metroid, samus, domain } )
					== metroid_series );
			}
		}
	}

	GIVEN( "a mapped tag that is aliased after its parent was declared" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus" ) };
		const auto samus_aran { insertTag( tx, "character", "samus aran" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };

		insertParent( tx, domain, metroid, samus );
		insertMapping( tx, record, samus, domain );

		WHEN( "the mapped tag is aliased onward" )
		{
			insertAlias( tx, domain, samus, samus_aran );

			THEN( "the record holds the alias target and keeps the parent" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus_aran, metroid } ) );
			}

			THEN( "the parent is now held up by the tag the mapping resolves to" )
			{
				CHECK( parentRows( tx, record, domain ) == std::vector { ParentRow { metroid, samus_aran, 0 } } );
			}

			AND_WHEN( "the mapping is withdrawn" )
			{
				removeMapping( tx, record, samus, domain );

				THEN( "the parent goes with it" )
				{
					CHECK( finalTags( tx, record, domain ).empty() );
					CHECK( parentRows( tx, record, domain ).empty() );
				}
			}
		}
	}

	GIVEN( "a parent declared over a tag that is already aliased" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus" ) };
		const auto samus_aran { insertTag( tx, "character", "samus aran" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };

		insertAlias( tx, domain, samus, samus_aran );
		insertMapping( tx, record, samus, domain );

		REQUIRE( finalTags( tx, record, domain ) == std::vector { samus_aran } );

		WHEN( "the parent is declared against the tag that was aliased away" )
		{
			insertParent( tx, domain, metroid, samus );

			THEN( "it still reaches the record through the alias" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus_aran, metroid } ) );
			}

			AND_WHEN( "the mapping is withdrawn" )
			{
				removeMapping( tx, record, samus, domain );

				THEN( "the parent goes with it" )
				{
					CHECK( finalTags( tx, record, domain ).empty() );
					CHECK( parentRows( tx, record, domain ).empty() );
				}
			}
		}

		WHEN( "the parent is declared against the tag the alias points at" )
		{
			insertParent( tx, domain, metroid, samus_aran );

			THEN( "it reaches the record" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus_aran, metroid } ) );
			}
		}
	}

	GIVEN( "a mapped tag under two grandparents, both aliased onto one tag at once" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus aran" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };
		const auto nintendo { insertTag( tx, "studio", "nintendo" ) };
		const auto nintendo_ead { insertTag( tx, "studio", "nintendo ead" ) };
		const auto nintendo_co { insertTag( tx, "studio", "nintendo co" ) };

		insertParent( tx, domain, metroid, samus );
		insertParent( tx, domain, nintendo, metroid );
		insertParent( tx, domain, nintendo_ead, metroid );
		insertMapping( tx, record, samus, domain );

		REQUIRE( internalCount( tx, record, nintendo, metroid, domain ) == 1 );
		REQUIRE( internalCount( tx, record, nintendo_ead, metroid, domain ) == 1 );

		WHEN( "both grandparents are aliased onto the same tag in one statement" )
		{
			tx.exec(
				"INSERT INTO tag_aliases (aliased_id, alias_id, tag_domain_id) VALUES ($1, $3, $4), ($2, $3, $4)",
				pqxx::params { nintendo, nintendo_ead, nintendo_co, domain } );

			THEN( "the two rows collapse onto one, carrying the sum of what they held" )
			{
				CHECK( internalCount( tx, record, nintendo_co, metroid, domain ) == 2 );
				CHECK( internalCount( tx, record, nintendo, metroid, domain ) == std::nullopt );
				CHECK( internalCount( tx, record, nintendo_ead, metroid, domain ) == std::nullopt );
			}

			THEN( "the record holds the alias target once instead of the two it replaced" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus, metroid, nintendo_co } ) );
				CHECK( finalTagRows( tx, record, domain ) == sorted( { samus, metroid, nintendo_co } ) );
			}

			AND_WHEN( "the mapping under all of it is withdrawn" )
			{
				removeMapping( tx, record, samus, domain );

				THEN( "everything it was holding up unwinds" )
				{
					CHECK( finalTags( tx, record, domain ).empty() );
					CHECK( parentRows( tx, record, domain ).empty() );
				}
			}
		}
	}
}

} // namespace idhan::test
