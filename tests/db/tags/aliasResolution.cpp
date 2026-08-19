#include <catch2/catch_test_macros.hpp>

#include "MappingHelpers.hpp"
#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

SCENARIO_METHOD( MigratedSchema, "Alias resolution of tag mappings", "[db][tags][aliases][mappings]" )
{
	pqxx::work tx { connection() };

	GIVEN( "a domain, a record, and two tags" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus" ) };
		const auto samus_aran { insertTag( tx, "character", "samus aran" ) };

		WHEN( "the tag is mapped with no alias standing over it" )
		{
			insertMapping( tx, record, samus, domain );

			THEN( "the record carries the tag it was given" )
			{
				CHECK( finalTags( tx, record, domain ) == std::vector { samus } );
				CHECK( effectiveTag( tx, record, samus, domain ) == samus );
			}
		}

		WHEN( "the alias is made before the mapping" )
		{
			insertAlias( tx, domain, samus, samus_aran );
			insertMapping( tx, record, samus, domain );

			THEN( "the mapping lands already pointing at the alias target" )
			{
				CHECK( finalTags( tx, record, domain ) == std::vector { samus_aran } );
				CHECK( effectiveTag( tx, record, samus, domain ) == samus_aran );
			}
		}

		WHEN( "the alias is made after the mapping" )
		{
			insertMapping( tx, record, samus, domain );
			REQUIRE( finalTags( tx, record, domain ) == std::vector { samus } );

			insertAlias( tx, domain, samus, samus_aran );

			THEN( "the mapping that was already there is retargeted" )
			{
				CHECK( finalTags( tx, record, domain ) == std::vector { samus_aran } );
				CHECK( effectiveTag( tx, record, samus, domain ) == samus_aran );
			}

			AND_WHEN( "the alias is removed" )
			{
				removeAlias( tx, domain, samus );

				THEN( "the mapping falls back to the tag it was given" )
				{
					CHECK( finalTags( tx, record, domain ) == std::vector { samus } );
					CHECK( effectiveTag( tx, record, samus, domain ) == samus );
				}
			}
		}

		WHEN( "the tag is mapped to a record that holds no file" )
		{
			const auto fileless { insertRecordWithoutFile( tx, 2 ) };
			insertMapping( tx, fileless, samus, domain );

			THEN( "the mapping never becomes active" )
			{
				CHECK( finalTags( tx, fileless, domain ).empty() );
				CHECK( effectiveTag( tx, fileless, samus, domain ) == std::nullopt );
			}
		}
	}

	GIVEN( "a mapped tag at the near end of an alias chain" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus" ) };
		const auto samus_aran { insertTag( tx, "character", "samus aran" ) };
		const auto samus_aran_metroid { insertTag( tx, "character", "samus aran (metroid)" ) };

		insertAlias( tx, domain, samus, samus_aran );
		insertMapping( tx, record, samus, domain );

		REQUIRE( finalTags( tx, record, domain ) == std::vector { samus_aran } );

		WHEN( "the far end of the chain is itself aliased onward" )
		{
			insertAlias( tx, domain, samus_aran, samus_aran_metroid );

			THEN( "the chain is compressed and the mapping follows it to the end" )
			{
				CHECK( finalTags( tx, record, domain ) == std::vector { samus_aran_metroid } );
				CHECK( effectiveTag( tx, record, samus, domain ) == samus_aran_metroid );
			}

			THEN( "the first alias row now names the end of the chain as its ideal" )
			{
				CHECK(
					tx.query_value< int >(
						"SELECT effective_tag_id FROM tag_aliases WHERE aliased_id = $1 AND tag_domain_id = $2",
						pqxx::params { samus, domain } )
					== samus_aran_metroid );
			}
		}

		WHEN( "the chain is closed back on itself" )
		{
			THEN( "the cycle is refused" )
			{
				CHECK_THROWS( insertAlias( tx, domain, samus_aran, samus ) );
			}
		}
	}

	GIVEN( "the same tag aliased in one domain and mapped in another" )
	{
		const auto aliasing_domain { insertDomain( tx, "characters" ) };
		const auto mapping_domain { insertDomain( tx, "series" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus" ) };
		const auto samus_aran { insertTag( tx, "character", "samus aran" ) };

		insertAlias( tx, aliasing_domain, samus, samus_aran );

		WHEN( "the mapping is made in the domain that carries no alias" )
		{
			insertMapping( tx, record, samus, mapping_domain );

			THEN( "the alias from the other domain does not reach it" )
			{
				CHECK( finalTags( tx, record, mapping_domain ) == std::vector { samus } );
				CHECK( effectiveTag( tx, record, samus, mapping_domain ) == samus );
			}

			THEN( "the domain that carries the alias holds nothing for the record" )
			{
				CHECK( finalTags( tx, record, aliasing_domain ).empty() );
			}
		}
	}
}

} // namespace idhan::test
