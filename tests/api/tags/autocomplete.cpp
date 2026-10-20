#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"
#include "MimeIDs.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag autocomplete", "[api][tags][autocomplete]" )
{
	GIVEN( "no tags" )
	{
		WHEN( "anything is autocompleted" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "samus" } } ) };

			THEN( "the answer is an empty array rather than null" )
			{
				REQUIRE( completed.status == 200 );
				REQUIRE( completed.json.isArray() );
				CHECK( completed.json.empty() );
			}
		}
	}

	GIVEN( "a handful of tags sharing a prefix" )
	{
		api().createTags( { { "character", "samus aran" }, { "character", "samus" }, { "series", "metroid" } } );

		WHEN( "the prefix is autocompleted" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "samus" } } ) };

			THEN( "both matching tags come back and the unrelated one does not" )
			{
				REQUIRE( completed.status == 200 );
				REQUIRE( completed.json.size() == 2 );

				for ( const auto& tag : completed.json ) CHECK( tag[ "tag_text" ].asString().contains( "samus" ) );
			}

			THEN( "every entry carries the fields a client sorts on" )
			{
				for ( const auto& tag : completed.json )
				{
					CHECK( tag.isMember( "tag_id" ) );
					CHECK( tag.isMember( "tag_text" ) );
					CHECK( tag.isMember( "similarity" ) );
					CHECK( tag.isMember( "count" ) );
				}
			}
		}

		WHEN( "an exact tag text is autocompleted" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "character:samus" } } ) };

			THEN( "the exact match sorts first" )
			{
				REQUIRE( completed.status == 200 );
				REQUIRE( completed.json.size() >= 1 );
				CHECK( completed.json[ 0 ][ "tag_text" ].asString() == "character:samus" );
			}
		}

		WHEN( "the limit is one" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "samus" }, { "limit", "1" } } ) };

			THEN( "one tag comes back" )
			{
				REQUIRE( completed.status == 200 );
				CHECK( completed.json.size() == 1 );
			}
		}

		WHEN( "unused tags are excluded" )
		{
			const auto completed {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "include_unused", "false" } } )
			};

			THEN( "nothing comes back, since none of them are on a record" )
			{
				REQUIRE( completed.status == 200 );
				CHECK( completed.json.empty() );
			}
		}

		WHEN( "the search is negated with a leading dash" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "-samus" } } ) };

			THEN( "the dash is stripped before matching" )
			{
				REQUIRE( completed.status == 200 );
				CHECK( completed.json.size() == 2 );
			}
		}

		WHEN( "an unknown display type is asked for" )
		{
			const auto completed {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "tag_display_type", "sideways" } } )
			};

			THEN( "the request is rejected" )
			{
				CHECK( completed.status == 400 );
			}
		}

		// Unused tags have zero counts in both mapping modes.
		WHEN( "storage and display are both asked for" )
		{
			const auto storage {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "tag_display_type", "storage" } } )
			};
			const auto display {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "tag_display_type", "display" } } )
			};

			THEN( "both modes include the same unused tags" )
			{
				REQUIRE( storage.status == 200 );
				REQUIRE( display.status == 200 );
				CHECK( storage.json == display.json );
			}
		}
	}
}

} // namespace idhan::test

namespace idhan::test
{

class AutocompleteScopeFixture : public ServerFixture
{
  protected:

	TagDomainID first { api().createDomain( "autocomplete first" ) };
	TagDomainID second { api().createDomain( "autocomplete second" ) };
	TagDomainID outside { api().createDomain( "autocomplete outside" ) };
	TagDomainID empty { api().createDomain( "autocomplete empty" ) };
	TagID applied { api().createTag( "scope", "alpha" ) };
	TagID canonical { api().createTag( "scope", "canon" ) };
	TagID parent { api().createTag( "scope", "parent" ) };
	TagID other { api().createTag( "scope", "bravo" ) };
	TagID unused { api().createTag( "scope", "unused" ) };

	ApiResponse complete(
		const std::vector< TagDomainID >& domains,
		const std::string& mode = "display",
		const bool include_unused = true ) const
	{
		QueryParams params {
			{ "tag", "scope:" }, { "tag_display_type", mode }, { "include_unused", include_unused ? "true" : "false" }
		};
		for ( const auto domain : domains ) params.emplace_back( "tag_domains", std::to_string( domain ) );
		return api().get( "/tags/autocomplete", params );
	}

	static std::int64_t countFor( const ApiResponse& response, const TagID tag )
	{
		REQUIRE( response.status == 200 );
		for ( const auto& row : response.json )
			if ( row[ "tag_id" ].asInt() == tag ) return row[ "count" ].asInt64();
		FAIL( "Expected tag is missing from autocomplete" );
		return -1;
	}

  public:

	AutocompleteScopeFixture()
	{
		const auto records { api().createRecords( { 170, 171, 172, 173, 174 } ) };
		{
			pqxx::work tx { db() };
			for ( const auto record : records )
				tx.exec(
					"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, cluster_delete_time) "
					"VALUES ($1, 123, $2, now(), now())",
					pqxx::params { record, mime_ids::IMAGE_PNG } );
			tx.commit();
		}
		for ( const auto domain : { first, second, outside } )
		{
			REQUIRE( api().createAliases( domain, { { applied, canonical } } ).status == 200 );
			REQUIRE( api().createParents( domain, { { parent, canonical } } ).status == 200 );
		}
		REQUIRE( api().addTags( first, { records[ 0 ], records[ 1 ], records[ 2 ] }, { applied } ).status == 200 );
		REQUIRE( api().addTags( second, { records[ 0 ], records[ 1 ] }, { applied } ).status == 200 );
		REQUIRE( api().addTags( outside, records, { applied } ).status == 200 );
		REQUIRE( api().addTags( first, { records[ 4 ] }, { other } ).status == 200 );
		REQUIRE(
			api().addTags( second, { records[ 0 ], records[ 1 ], records[ 2 ], records[ 3 ] }, { other } ).status
			== 200 );
	}
};

TEST_CASE_METHOD(
	AutocompleteScopeFixture,
	"Autocomplete selects the domain and mapping mode consistently",
	"[api][tags][autocomplete][scope]" )
{
	const auto storage { complete( { first }, "storage" ) };
	CHECK( countFor( storage, applied ) == 3 );
	CHECK( countFor( storage, canonical ) == 0 );
	CHECK( countFor( storage, parent ) == 0 );
	CHECK( countFor( storage, other ) == 1 );
	CHECK( countFor( storage, unused ) == 0 );
	const auto display { complete( { first } ) };
	CHECK( countFor( display, applied ) == 0 );
	CHECK( countFor( display, canonical ) == 3 );
	CHECK( countFor( display, parent ) == 3 );
	CHECK( countFor( display, other ) == 1 );
	CHECK(
		api().get( "/tags/autocomplete", { { "tag", "scope:" }, { "tag_domains", std::to_string( first ) } } ).json
		== display.json );
	CHECK( complete( { first }, "storage", false ).json.size() == 2 );
	CHECK( complete( { first }, "display", false ).json.size() == 3 );
	const auto no_counts { complete( { empty } ) };
	CHECK( countFor( no_counts, applied ) == 0 );
	CHECK( countFor( no_counts, canonical ) == 0 );
	CHECK( complete( { empty }, "display", false ).json.empty() );
}

TEST_CASE_METHOD(
	AutocompleteScopeFixture,
	"Autocomplete uses the maximum over only the requested domains",
	"[api][tags][autocomplete][scope]" )
{
	const auto selected { complete( { first, second }, "storage" ) };
	CHECK( countFor( selected, applied ) == 3 );
	CHECK( countFor( selected, other ) == 4 );
	REQUIRE( selected.json.size() >= 2 );
	CHECK( selected.json[ 0 ][ "tag_id" ].asInt() == other );
	CHECK( selected.json[ 1 ][ "tag_id" ].asInt() == applied );
	CHECK( complete( { second, first, second }, "storage" ).json == selected.json );
	CHECK( countFor( complete( { second }, "storage" ), applied ) == 2 );
	CHECK( countFor( complete( {}, "storage" ), applied ) == 5 );
	const auto selected_display { complete( { first, second } ) };
	CHECK( countFor( selected_display, canonical ) == 3 );
	CHECK( countFor( selected_display, parent ) == 3 );
	CHECK(
		api().get( "/tags/autocomplete", { { "tag", "scope:" }, { "tag_domain", std::to_string( first ) } } ).json
		== complete( { first } ).json );
	CHECK(
		api()
			.get(
				"/tags/autocomplete",
				{ { "tag", "scope:" },
	              { "tag_domains", std::to_string( first ) },
	              { "tag_domain", std::to_string( outside ) } } )
			.json
		== complete( { first } ).json );
	const auto exact {
		api().get( "/tags/autocomplete", { { "tag", "scope:alpha" }, { "tag_domains", std::to_string( first ) } } )
	};
	REQUIRE( exact.status == 200 );
	CHECK( exact.json[ 0 ][ "tag_id" ].asInt() == applied );
	CHECK( exact.json[ 0 ][ "count" ].asInt64() == 0 );
	CHECK(
		countFor(
			api().get( "/tags/autocomplete", { { "tag", "-scope:" }, { "tag_domains", std::to_string( first ) } } ),
			canonical )
		== 3 );
}

TEST_CASE_METHOD(
	AutocompleteScopeFixture,
	"Autocomplete rejects malformed or nonexistent domain selections",
	"[api][tags][autocomplete][scope]" )
{
	for ( const std::string value : { "0", "-1", "32768", "65537", "1.5", "abc", "" } )
	{
		CAPTURE( value );
		CHECK( api().get( "/tags/autocomplete", { { "tag", "scope:" }, { "tag_domains", value } } ).status == 400 );
	}
	CHECK( complete( { 32767 } ).status == 404 );
	CHECK( complete( { first, 32767 } ).status == 404 );
	CHECK( api().get( "/tags/autocomplete", { { "tag", "scope:" }, { "include_unused", "maybe" } } ).status == 400 );
}

} // namespace idhan::test
