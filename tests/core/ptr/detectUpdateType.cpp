#include <gtest/gtest.h>

#include <json/json.h>

#include "ptr/PTRConstants.hpp"
#include "ptr/PTRFileParser.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

//! A PTR update file's JSON root is [ serialisable_type, version, serialisable_info ].
Json::Value makeRoot( const int serialisable_type )
{
	Json::Value root { Json::arrayValue };
	root.append( serialisable_type );
	root.append( 1 );
	root.append( Json::Value( Json::arrayValue ) );
	return root;
}

TEST( PTRDetectUpdateType, RecognisesContent )
{
	EXPECT_EQ( detectUpdateType( makeRoot( SERIALISABLE_TYPE_CONTENT_UPDATE ) ), UpdateType::Content );
}

TEST( PTRDetectUpdateType, RecognisesDefinitions )
{
	EXPECT_EQ( detectUpdateType( makeRoot( SERIALISABLE_TYPE_DEFINITIONS_UPDATE ) ), UpdateType::Definitions );
}

TEST( PTRDetectUpdateType, RecognisesMetadata )
{
	EXPECT_EQ( detectUpdateType( makeRoot( SERIALISABLE_TYPE_METADATA ) ), UpdateType::Metadata );
}

TEST( PTRDetectUpdateType, UnknownTypeIsUnknown )
{
	EXPECT_EQ( detectUpdateType( makeRoot( 9999 ) ), UpdateType::Unknown );
}

} // namespace
