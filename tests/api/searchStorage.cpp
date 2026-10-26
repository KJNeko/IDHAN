#include <catch2/catch_test_macros.hpp>

#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

static RecordIDs storageSearchResults( ApiClient& api, const Json::Value& body )
{
	const auto response { api.post( "/search", body ) };
	if ( response.status != 200 ) throw std::runtime_error( std::format( "Search failed: {}", response.body ) );

	RecordIDs ids {};
	for ( const auto& id : response.json[ "record_ids" ] ) ids.push_back( id.asInt() );
	return ids;
}

class StorageSearchFixture : public ServerFixture
{
  protected:

	TagDomainID domain { api().createDomain( "storage search" ) };
	TagDomainID other_domain { api().createDomain( "other storage search" ) };
	TagID old_tag { api().createTag( "old", "name" ) };
	TagID canonical { api().createTag( "character", "name" ) };
	TagID parent { api().createTag( "species", "parent" ) };
	TagID grandparent { api().createTag( "group", "ancestor" ) };
	TagID first_direct { api().createTag( "direct", "first" ) };
	TagID second_direct { api().createTag( "direct", "second" ) };
	RecordID aliased { api().createRecord( 61 ) };
	RecordID direct { api().createRecord( 62 ) };
	RecordID untagged { api().createRecord( 63 ) };
	RecordID other { api().createRecord( 64 ) };
	RecordID inactive { api().createRecord( 65 ) };

	[[nodiscard]] Json::Value searchBody( const std::optional< std::string >& display = "storage" ) const
	{
		Json::Value body { Json::objectValue };
		if ( display ) body[ "display" ] = *display;
		body[ "tag_domains" ] = Json::Value { Json::arrayValue };
		body[ "tag_domains" ].append( domain );
		body[ "sort" ][ "by" ] = "record_time";
		body[ "sort" ][ "order" ] = "asc";
		return body;
	}

	[[nodiscard]] RecordIDs searchTag( const TagID tag_id, const std::optional< std::string >& display ) const
	{
		auto body { searchBody( display ) };
		body[ "tag_ids" ] = Json::Value { Json::arrayValue };
		body[ "tag_ids" ].append( tag_id );
		return storageSearchResults( api(), body );
	}

	[[nodiscard]] RecordIDs searchText( const std::string& text, const std::string& display ) const
	{
		auto body { searchBody( display ) };
		body[ "tags" ] = Json::Value { Json::arrayValue };
		body[ "tags" ].append( text );
		return storageSearchResults( api(), body );
	}

  public:

	StorageSearchFixture()
	{
		{
			pqxx::work tx { db() };
			for ( const auto record_id : { aliased, direct, untagged, other } )
				tx.exec(
					"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, cluster_delete_time) "
					"VALUES ($1, 123, $2, now(), now())",
					pqxx::params { record_id, mime_ids::IMAGE_PNG } );
			tx.commit();
		}

		REQUIRE( api().createAliases( domain, { { old_tag, canonical } } ).status == 200 );
		REQUIRE( api().createParents( domain, { { parent, canonical }, { grandparent, parent } } ).status == 200 );
		REQUIRE( api().addTags( domain, { aliased, inactive }, { old_tag } ).status == 200 );
		REQUIRE( api().addTags( domain, { direct }, { first_direct, second_direct } ).status == 200 );
		REQUIRE( api().addTags( other_domain, { other }, { old_tag } ).status == 200 );
	}
};

TEST_CASE_METHOD( StorageSearchFixture, "Storage search uses directly applied tag IDs", "[api][search][storage]" )
{
	CHECK( searchTag( old_tag, "storage" ) == RecordIDs { aliased } );
	CHECK( searchTag( canonical, "storage" ).empty() );
	CHECK( searchTag( parent, "storage" ).empty() );
	CHECK( searchTag( old_tag, "display" ).empty() );
	CHECK( searchTag( canonical, "display" ) == RecordIDs { aliased } );
	CHECK( searchTag( parent, "display" ) == RecordIDs { aliased } );
	CHECK( searchTag( canonical, std::nullopt ) == RecordIDs { aliased } );
	CHECK( searchTag( old_tag, std::nullopt ).empty() );
}

TEST_CASE_METHOD(
	StorageSearchFixture,
	"Storage search applies its mode to text and wildcard terms",
	"[api][search][storage]" )
{
	CHECK( searchText( "old:name", "storage" ) == RecordIDs { aliased } );
	CHECK( searchText( "old:name", "display" ).empty() );
	CHECK( searchText( "character:name", "storage" ).empty() );
	CHECK( searchText( "character:name", "display" ) == RecordIDs { aliased } );
	CHECK( searchText( "old:n*", "storage" ) == RecordIDs { aliased } );
	CHECK( searchText( "old:n*", "display" ).empty() );
	CHECK( searchText( "species:p*", "storage" ).empty() );
	CHECK( searchText( "species:p*", "display" ) == RecordIDs { aliased } );
	CHECK( searchText( "old:*", "storage" ) == RecordIDs { aliased } );
	CHECK( searchText( "old:*", "display" ).empty() );
	CHECK( searchText( "species:*", "storage" ).empty() );
	CHECK( searchText( "species:*", "display" ) == RecordIDs { aliased } );

	CHECK( searchText( "-old:name", "storage" ) == RecordIDs { direct, untagged, other } );
	CHECK( searchText( "-old:name", "display" ) == RecordIDs { aliased, direct, untagged, other } );
	CHECK( searchText( "-species:p*", "storage" ) == RecordIDs { aliased, direct, untagged, other } );
	CHECK( searchText( "-species:p*", "display" ) == RecordIDs { direct, untagged, other } );
}

TEST_CASE_METHOD(
	StorageSearchFixture,
	"Storage search preserves domains and distinct records",
	"[api][search][storage]" )
{
	auto body { searchBody() };
	body[ "tag_ids" ] = Json::Value { Json::arrayValue };
	body[ "tag_ids" ].append( old_tag );
	CHECK( storageSearchResults( api(), body ) == RecordIDs { aliased } );
	body[ "tag_domains" ][ 0 ] = other_domain;
	CHECK( storageSearchResults( api(), body ) == RecordIDs { other } );
	body.removeMember( "tag_domains" );
	CHECK( storageSearchResults( api(), body ) == RecordIDs { aliased, other } );

	REQUIRE( api().addTags( other_domain, { aliased }, { old_tag } ).status == 200 );
	CHECK( storageSearchResults( api(), body ) == RecordIDs { aliased, other } );
	body[ "tags" ] = Json::Value { Json::arrayValue };
	body[ "tags" ].append( "old:name" );
	body[ "tags" ].append( "system:number of tags = 1" );
	CHECK( storageSearchResults( api(), body ) == RecordIDs { aliased, other } );
	body[ "return" ] = Json::Value { Json::arrayValue };
	body[ "return" ].append( "ids" );
	body[ "return" ].append( "hashes" );
	body[ "offset" ] = 1;
	body[ "limit" ] = 1;
	const auto response { api().post( "/search", body ) };
	REQUIRE( response.status == 200 );
	REQUIRE( response.json[ "record_ids" ].size() == 1 );
	CHECK( response.json[ "record_ids" ][ 0 ].asInt() == other );
	REQUIRE( response.json[ "hashes" ].size() == 1 );
	CHECK( response.json[ "hashes" ][ 0 ].asString() == hashFor( 64 ) );
}

TEST_CASE_METHOD( StorageSearchFixture, "Tag counts and count sorting follow storage mode", "[api][search][storage]" )
{
	CHECK( searchText( "system:number of tags = 1", "storage" ) == RecordIDs { aliased, other } );
	CHECK( searchText( "system:number of tags = 1", "display" ) == RecordIDs { other } );
	CHECK( searchText( "system:number of tags = 3", "storage" ).empty() );
	CHECK( searchText( "system:number of tags = 3", "display" ) == RecordIDs { aliased } );
	CHECK( searchText( "system:has tags", "storage" ) == RecordIDs { aliased, direct, other } );
	CHECK( searchText( "system:no tags", "storage" ) == RecordIDs { untagged } );
	CHECK( searchText( "system:untagged", "display" ) == RecordIDs { untagged } );

	for ( const auto& display : { "storage", "display" } )
	{
		auto body { searchBody( display ) };
		body[ "sort" ][ "by" ] = "num_tags";
		body[ "offset" ] = 2;
		body[ "limit" ] = 1;
		CHECK(
			storageSearchResults( api(), body )
			== RecordIDs { display == std::string_view { "storage" } ? other : direct } );

		body[ "tags" ] = Json::Value { Json::arrayValue };
		body[ "tags" ].append( "system:has tags" );
		body[ "offset" ] = 1;
		CHECK(
			storageSearchResults( api(), body )
			== RecordIDs { display == std::string_view { "storage" } ? other : direct } );

		body[ "tags" ][ 0 ] = "-direct:f*";
		body[ "offset" ] = 2;
		CHECK(
			storageSearchResults( api(), body )
			== RecordIDs { display == std::string_view { "storage" } ? other : aliased } );
	}
}

} // namespace idhan::test
