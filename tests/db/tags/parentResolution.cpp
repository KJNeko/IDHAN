#include <catch2/catch_test_macros.hpp>

#include "MappingHelpers.hpp"
#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

SCENARIO_METHOD( MigratedSchema, "Parent resolution of tag mappings", "[db][tags][parents][mappings]" )
{
	pqxx::work tx { connection() };

	GIVEN( "a domain, a record, and a tag under a parent" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus aran" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };

		WHEN( "the parent is declared before the mapping" )
		{
			insertParent( tx, domain, metroid, samus );
			insertMapping( tx, record, samus, domain );

			THEN( "the record carries the parent alongside the tag it was given" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus, metroid } ) );
				CHECK( parentRows( tx, record, domain ) == std::vector { ParentRow { metroid, samus, 0 } } );
			}
		}

		WHEN( "the parent is declared after the mapping" )
		{
			insertMapping( tx, record, samus, domain );
			REQUIRE( finalTags( tx, record, domain ) == std::vector { samus } );

			insertParent( tx, domain, metroid, samus );

			THEN( "the parent reaches the mapping that was already there" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus, metroid } ) );
				CHECK( parentRows( tx, record, domain ) == std::vector { ParentRow { metroid, samus, 0 } } );
			}

			AND_WHEN( "the parent is withdrawn" )
			{
				removeParent( tx, domain, metroid, samus );

				THEN( "only the tag the record was given is left" )
				{
					CHECK( finalTags( tx, record, domain ) == std::vector { samus } );
					CHECK( parentRows( tx, record, domain ).empty() );
				}
			}

			AND_WHEN( "the mapping is withdrawn" )
			{
				removeMapping( tx, record, samus, domain );

				THEN( "the parent it was holding up goes with it" )
				{
					CHECK( finalTags( tx, record, domain ).empty() );
					CHECK( parentRows( tx, record, domain ).empty() );
				}
			}
		}
	}

	GIVEN( "a two level parent chain" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus aran" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };
		const auto nintendo { insertTag( tx, "studio", "nintendo" ) };

		WHEN( "the chain is built before the mapping" )
		{
			insertParent( tx, domain, metroid, samus );
			insertParent( tx, domain, nintendo, metroid );
			insertMapping( tx, record, samus, domain );

			THEN( "the whole chain lands, the grandparent held internally" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus, metroid, nintendo } ) );
				CHECK(
					parentRows( tx, record, domain )
					== std::vector { ParentRow { metroid, samus, 0 }, ParentRow { nintendo, metroid, 1 } } );
			}
		}

		WHEN( "the chain is built after the mapping, one level at a time" )
		{
			insertMapping( tx, record, samus, domain );
			insertParent( tx, domain, metroid, samus );
			insertParent( tx, domain, nintendo, metroid );

			THEN( "it settles the same way as building it beforehand" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus, metroid, nintendo } ) );
				CHECK(
					parentRows( tx, record, domain )
					== std::vector { ParentRow { metroid, samus, 0 }, ParentRow { nintendo, metroid, 1 } } );
			}
		}

		WHEN( "the mapping under a fully built chain is withdrawn" )
		{
			insertParent( tx, domain, metroid, samus );
			insertParent( tx, domain, nintendo, metroid );
			insertMapping( tx, record, samus, domain );
			REQUIRE( finalTags( tx, record, domain ).size() == 3 );

			removeMapping( tx, record, samus, domain );

			THEN( "the whole chain unwinds, the grandparent included" )
			{
				CHECK( finalTags( tx, record, domain ).empty() );
				CHECK( parentRows( tx, record, domain ).empty() );
			}
		}

		WHEN( "a cycle is closed at the top of the chain" )
		{
			insertParent( tx, domain, metroid, samus );
			insertParent( tx, domain, nintendo, metroid );

			THEN( "the cycle is refused" )
			{
				CHECK_THROWS( insertParent( tx, domain, samus, nintendo ) );
			}
		}
	}

	GIVEN( "one parent standing over two mapped tags" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus aran" ) };
		const auto ridley { insertTag( tx, "character", "ridley" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };

		insertParent( tx, domain, metroid, samus );
		insertParent( tx, domain, metroid, ridley );
		insertMapping( tx, record, samus, domain );
		insertMapping( tx, record, ridley, domain );

		WHEN( "both mappings stand" )
		{
			THEN( "the parent is held once per mapping that asks for it" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { samus, ridley, metroid } ) );
				CHECK(
					parentRows( tx, record, domain )
					== std::vector { ParentRow { metroid, samus, 0 }, ParentRow { metroid, ridley, 0 } } );
			}

			THEN( "a reader of the view sees the parent once per holder" )
			{
				CHECK( finalTagRows( tx, record, domain ) == sorted( { samus, ridley, metroid, metroid } ) );
			}
		}

		WHEN( "one of the two mappings is withdrawn" )
		{
			removeMapping( tx, record, samus, domain );

			THEN( "the parent stays, held up by the one that is left" )
			{
				CHECK( finalTags( tx, record, domain ) == sorted( { ridley, metroid } ) );
				CHECK( parentRows( tx, record, domain ) == std::vector { ParentRow { metroid, ridley, 0 } } );
			}
		}

		WHEN( "both mappings are withdrawn" )
		{
			removeMapping( tx, record, samus, domain );
			removeMapping( tx, record, ridley, domain );

			THEN( "nothing is left holding the parent up" )
			{
				CHECK( finalTags( tx, record, domain ).empty() );
				CHECK( parentRows( tx, record, domain ).empty() );
			}
		}
	}

	GIVEN( "a parent declared in one domain and the child mapped in another" )
	{
		const auto parenting_domain { insertDomain( tx, "characters" ) };
		const auto mapping_domain { insertDomain( tx, "series" ) };
		const auto record { insertRecord( tx, 1 ) };
		const auto samus { insertTag( tx, "character", "samus aran" ) };
		const auto metroid { insertTag( tx, "series", "metroid" ) };

		insertParent( tx, parenting_domain, metroid, samus );

		WHEN( "the mapping is made in the domain that carries no parent" )
		{
			insertMapping( tx, record, samus, mapping_domain );

			THEN( "the parent from the other domain does not reach it" )
			{
				CHECK( finalTags( tx, record, mapping_domain ) == std::vector { samus } );
				CHECK( parentRows( tx, record, mapping_domain ).empty() );
			}
		}
	}
}

} // namespace idhan::test
