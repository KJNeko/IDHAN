#include <gtest/gtest.h>

#include "hyapi/constants/ServiceTypes.hpp"
#include "hyapi/services/Services.hpp"

using namespace idhan;
using namespace idhan::hyapi;

namespace gc = idhan::hydrus::gen_constants;

// The keys a real Hydrus client reports. IDHAN must issue these byte-for-byte.
TEST( HyAPIServiceKeys, MatchesHydrusWellKnownKeys )
{
	EXPECT_EQ( encodeServiceKey( "all known tags" ), "616c6c206b6e6f776e2074616773" );
	EXPECT_EQ( encodeServiceKey( "all known files" ), "616c6c206b6e6f776e2066696c6573" );
	EXPECT_EQ( encodeServiceKey( "all local files" ), "616c6c206c6f63616c2066696c6573" );
	EXPECT_EQ( encodeServiceKey( "all local media" ), "616c6c206c6f63616c206d65646961" );
	EXPECT_EQ( encodeServiceKey( "repository updates" ), "7265706f7369746f72792075706461746573" );
	EXPECT_EQ( encodeServiceKey( "trash" ), "7472617368" );
}

TEST( HyAPIServiceKeys, ResolvesWellKnownKeysToTheirType )
{
	const auto expect_type = []( const std::string_view key, const std::size_t type )
	{
		const auto ref { parseServiceKey( key ) };
		ASSERT_TRUE( ref.has_value() );
		EXPECT_EQ( ref->type, type );
		EXPECT_FALSE( ref->tag_domain.has_value() );
		EXPECT_FALSE( ref->cluster.has_value() );
	};

	expect_type( "616c6c206b6e6f776e2074616773", gc::COMBINED_TAG );
	expect_type( "616c6c206b6e6f776e2066696c6573", gc::COMBINED_FILE );
	expect_type( "616c6c206c6f63616c2066696c6573", gc::HYDRUS_LOCAL_FILE_STORAGE );
	expect_type( "616c6c206c6f63616c206d65646961", gc::COMBINED_LOCAL_FILE_DOMAINS );
	expect_type( "7265706f7369746f72792075706461746573", gc::LOCAL_FILE_UPDATE_DOMAIN );
	expect_type( "7472617368", gc::LOCAL_FILE_TRASH_DOMAIN );
}

TEST( HyAPIServiceKeys, RoundTripsTagDomains )
{
	for ( const TagDomainID id : { TagDomainID { 0 }, TagDomainID { 1 }, TagDomainID { 32767 } } )
	{
		const auto ref { parseServiceKey( tagDomainServiceKey( id ) ) };
		ASSERT_TRUE( ref.has_value() ) << "id " << id;
		EXPECT_EQ( ref->type, gc::LOCAL_TAG );
		ASSERT_TRUE( ref->tag_domain.has_value() );
		EXPECT_EQ( ref->tag_domain.value(), id );
		EXPECT_FALSE( ref->cluster.has_value() );
	}
}

TEST( HyAPIServiceKeys, RoundTripsFileClusters )
{
	for ( const ClusterID id : { ClusterID { 1 }, ClusterID { 9 }, ClusterID { 32767 } } )
	{
		const auto ref { parseServiceKey( fileClusterServiceKey( id ) ) };
		ASSERT_TRUE( ref.has_value() ) << "id " << id;
		EXPECT_EQ( ref->type, gc::LOCAL_FILE_DOMAIN );
		ASSERT_TRUE( ref->cluster.has_value() );
		EXPECT_EQ( ref->cluster.value(), id );
		EXPECT_FALSE( ref->tag_domain.has_value() );
	}
}

TEST( HyAPIServiceKeys, TagDomainAndClusterKeysNeverCollide )
{
	EXPECT_NE( tagDomainServiceKey( 1 ), fileClusterServiceKey( 1 ) );
}

TEST( HyAPIServiceKeys, RejectsKeysItDidNotIssue )
{
	EXPECT_FALSE( parseServiceKey( "" ).has_value() );
	EXPECT_FALSE( parseServiceKey( "5-1" ).has_value() );
	EXPECT_FALSE( parseServiceKey( "abc" ).has_value() );          // odd length
	EXPECT_FALSE( parseServiceKey( "zzzz" ).has_value() );         // not hex
	EXPECT_FALSE( parseServiceKey( encodeServiceKey( "idhan tag domain" ) ).has_value() );      // no id
	EXPECT_FALSE( parseServiceKey( encodeServiceKey( "idhan tag domain x" ) ).has_value() );    // non-numeric id
	EXPECT_FALSE( parseServiceKey( encodeServiceKey( "idhan tag domain 1x" ) ).has_value() );   // trailing junk
	EXPECT_FALSE( parseServiceKey( encodeServiceKey( "idhan tag domain 99999" ) ).has_value() );// out of range
	EXPECT_FALSE( parseServiceKey( encodeServiceKey( "my tags" ) ).has_value() );
}

TEST( HyAPIServiceKeys, AcceptsUppercaseHex )
{
	const auto ref { parseServiceKey( "616C6C206B6E6F776E2074616773" ) };
	ASSERT_TRUE( ref.has_value() );
	EXPECT_EQ( ref->type, gc::COMBINED_TAG );
}

static std::vector< ServiceInfo > sampleServices()
{
	return { ServiceInfo { .key = "aa",
		                   .name = "my tags",
		                   .type = gc::LOCAL_TAG,
		                   .type_pretty = servicePrettyName( gc::LOCAL_TAG ) },
		     ServiceInfo { .key = "bb",
		                   .name = "my files",
		                   .type = gc::LOCAL_FILE_DOMAIN,
		                   .type_pretty = servicePrettyName( gc::LOCAL_FILE_DOMAIN ) },
		     ServiceInfo { .key = "cc",
		                   .name = "all known tags",
		                   .type = gc::COMBINED_TAG,
		                   .type_pretty = servicePrettyName( gc::COMBINED_TAG ) } };
}

// Hydrus' `services` object carries the key only as the map key.
TEST( HyAPIServiceShapes, DictOmitsTheServiceKeyFromTheValue )
{
	const auto services { sampleServices() };
	const auto json { servicesDict( services ) };

	ASSERT_TRUE( json.isObject() );
	EXPECT_EQ( json.size(), 3 );

	ASSERT_TRUE( json.isMember( "aa" ) );
	EXPECT_FALSE( json[ "aa" ].isMember( "service_key" ) );
	EXPECT_EQ( json[ "aa" ][ "name" ].asString(), "my tags" );
	EXPECT_EQ( json[ "aa" ][ "type" ].asUInt64(), gc::LOCAL_TAG );
	EXPECT_EQ( json[ "aa" ][ "type_pretty" ].asString(), "local tag domain" );
}

// `services_v2` is a list, and each entry carries its own key.
TEST( HyAPIServiceShapes, ListCarriesTheServiceKey )
{
	const auto services { sampleServices() };
	const auto json { servicesList( services ) };

	ASSERT_TRUE( json.isArray() );
	ASSERT_EQ( json.size(), 3 );
	EXPECT_EQ( json[ 0 ][ "service_key" ].asString(), "aa" );
	EXPECT_EQ( json[ 0 ][ "name" ].asString(), "my tags" );
	EXPECT_EQ( json[ 2 ][ "service_key" ].asString(), "cc" );
}

TEST( HyAPIServiceShapes, OfTypeFiltersAndStaysAnArrayWhenEmpty )
{
	const auto services { sampleServices() };

	const auto tags { servicesOfType( services, gc::LOCAL_TAG ) };
	ASSERT_TRUE( tags.isArray() );
	ASSERT_EQ( tags.size(), 1 );
	EXPECT_EQ( tags[ 0 ][ "name" ].asString(), "my tags" );

	const auto repositories { servicesOfType( services, gc::TAG_REPOSITORY ) };
	EXPECT_TRUE( repositories.isArray() );
	EXPECT_EQ( repositories.size(), 0 );
}

TEST( HyAPIServiceShapes, FindsByName )
{
	const auto services { sampleServices() };

	const auto found { findServiceByName( services, "all known tags" ) };
	ASSERT_TRUE( found.has_value() );
	EXPECT_EQ( found->key, "cc" );

	EXPECT_FALSE( findServiceByName( services, "nonexistent" ).has_value() );
}

TEST( HyAPIServiceShapes, FindsByKey )
{
	const auto services { sampleServices() };

	const auto found { findServiceByKey( services, "bb" ) };
	ASSERT_TRUE( found.has_value() );
	EXPECT_EQ( found->name, "my files" );

	EXPECT_FALSE( findServiceByKey( services, "zz" ).has_value() );
}

// Mirrors HC.service_string_lookup.
TEST( HyAPIServiceTypes, PrettyNamesMatchHydrus )
{
	EXPECT_EQ( servicePrettyName( gc::LOCAL_TAG ), "local tag domain" );
	EXPECT_EQ( servicePrettyName( gc::TAG_REPOSITORY ), "hydrus tag repository" );
	EXPECT_EQ( servicePrettyName( gc::LOCAL_FILE_DOMAIN ), "local file domain" );
	EXPECT_EQ( servicePrettyName( gc::LOCAL_FILE_UPDATE_DOMAIN ), "local update file domain" );
	EXPECT_EQ( servicePrettyName( gc::FILE_REPOSITORY ), "hydrus file repository" );
	EXPECT_EQ( servicePrettyName( gc::HYDRUS_LOCAL_FILE_STORAGE ), "virtual combined local file domain" );
	EXPECT_EQ( servicePrettyName( gc::COMBINED_LOCAL_FILE_DOMAINS ), "virtual combined local media domain" );
	EXPECT_EQ( servicePrettyName( gc::COMBINED_FILE ), "virtual combined file domain" );
	EXPECT_EQ( servicePrettyName( gc::COMBINED_TAG ), "virtual combined tag domain" );
	EXPECT_EQ( servicePrettyName( gc::LOCAL_FILE_TRASH_DOMAIN ), "local trash file domain" );
}
