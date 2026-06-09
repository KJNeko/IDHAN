#include <QCoreApplication>

#include <gtest/gtest.h>

#include "NET_CONSTANTS.hpp"
#include "helpers/serverStarterHelper.hpp"
#include "idhan/IDHANClient.hpp"

template < typename T >
static void qtWaitFuture( QFuture< T >& future )
{
	while ( !future.isFinished() ) QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
}

struct ClientAPITests : public ::testing::Test
{
	int argc { 0 };
	std::unique_ptr< QCoreApplication > app;
	std::unique_ptr< ServerHandle > server_handle;
	std::unique_ptr< idhan::IDHANClient > client;

	idhan::TagDomainID default_domain { 0 };

	void SetUp() override
	{
		app = std::make_unique< QCoreApplication >( argc, nullptr );
		server_handle = std::make_unique< ServerHandle >( startServer() );
		client =
			std::make_unique< idhan::IDHANClient >( "test", "localhost", idhan::IDHAN_DEFAULT_PORT, "testkey", false );

		auto domain_future { client->getTagDomain( "default" ) };
		qtWaitFuture( domain_future );
		auto domain_opt { domain_future.result() };
		if ( domain_opt.has_value() )
		{
			default_domain = domain_opt.value();
		}
		else
		{
			auto create_future { client->createTagDomain( "default" ) };
			qtWaitFuture( create_future );
			default_domain = create_future.result();
		}
	}
};

TEST_F( ClientAPITests, GetTagInfo )
{
	auto tag_future { client->createTag( "test:api_info" ) };
	qtWaitFuture( tag_future );
	ASSERT_GT( tag_future.resultCount(), 0 );
	const auto tag_id { tag_future.result() };

	auto info_future { client->getTagInfo( tag_id ) };
	qtWaitFuture( info_future );
	ASSERT_GT( info_future.resultCount(), 0 );

	const auto info { info_future.result() };
	ASSERT_EQ( info.m_id, tag_id );
	ASSERT_EQ( info.m_subtag.m_text, "api_info" );
	ASSERT_EQ( info.m_namespace.m_text, "test" );
}

TEST_F( ClientAPITests, AutocompleteTag )
{
	auto tag_future { client->createTag( "test:autocomplete_me" ) };
	qtWaitFuture( tag_future );
	ASSERT_TRUE( tag_future.result() > 0 );

	auto auto_future { client->autocompleteTag( "test:autocomplete" ) };
	qtWaitFuture( auto_future );
	ASSERT_GT( auto_future.resultCount(), 0 );

	const auto results { auto_future.result() };
	ASSERT_GT( results.size(), 0 );

	bool found { false };
	for ( const auto& [ id, text ] : results )
	{
		if ( text == "test:autocomplete_me" )
		{
			found = true;
			break;
		}
	}
	ASSERT_TRUE( found );
}

TEST_F( ClientAPITests, TagDomainCRUD )
{
	const std::string domain_name { "test_domain_crud" };
	auto create_future { client->createTagDomain( domain_name ) };
	qtWaitFuture( create_future );
	ASSERT_GT( create_future.resultCount(), 0 );
	const auto domain_id { create_future.result() };
	ASSERT_GT( domain_id, 0 );

	auto get_future { client->getTagDomain( domain_name ) };
	qtWaitFuture( get_future );
	const auto retrieved { get_future.result() };
	ASSERT_TRUE( retrieved.has_value() );
	ASSERT_EQ( retrieved.value(), domain_id );
}

TEST_F( ClientAPITests, GetTagInfoNonexistent )
{
	auto info_future { client->getTagInfo( 999999 ) };
	qtWaitFuture( info_future );
	ASSERT_EQ( info_future.resultCount(), 0 );
	EXPECT_THROW( info_future.result(), std::runtime_error );
}

TEST_F( ClientAPITests, AddTagsToRecord )
{
	auto tag_future { client->createTag( "test:record_tag" ) };
	qtWaitFuture( tag_future );
	const auto tag_id { tag_future.result() };

	// Create a record
	std::array< std::byte, 32 > test_hash {};
	std::vector< std::array< std::byte, 32 > > test_hashes { test_hash };

	auto record_future { client->createRecords( test_hashes ) };
	qtWaitFuture( record_future );
	const auto record_ids { record_future.result() };
	ASSERT_EQ( record_ids.size(), 1 );
	const auto record_id { record_ids[ 0 ] };

	// Add tag to the record
	std::vector< std::pair< std::string, std::string > > tags { { "test", "record_tag" } };
	auto add_future { client->addTags( record_id, default_domain, std::move( tags ) ) };
	qtWaitFuture( add_future );

	// Verify tag is on the record
	auto list_future { client->getRecordTags( record_id, default_domain ) };
	qtWaitFuture( list_future );
	const auto tag_ids { list_future.result() };
	ASSERT_GT( tag_ids.size(), 0 );

	bool found { false };
	for ( const auto& tid : tag_ids )
	{
		if ( tid == tag_id )
		{
			found = true;
			break;
		}
	}
	ASSERT_TRUE( found );
}

TEST_F( ClientAPITests, AddTagsToRecordByString )
{
	auto record_future {
		client->createRecords( { "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa01" } )
	};
	qtWaitFuture( record_future );
	const auto record_ids { record_future.result() };
	ASSERT_EQ( record_ids.size(), 1 );

	std::vector< std::pair< std::string, std::string > > tags { { "test", "string_tag" } };
	auto add_future { client->addTags( record_ids[ 0 ], default_domain, std::move( tags ) ) };
	qtWaitFuture( add_future );

	auto list_future { client->getRecordTags( record_ids[ 0 ], default_domain ) };
	qtWaitFuture( list_future );
	const auto tag_ids { list_future.result() };
	ASSERT_GT( tag_ids.size(), 0 );
}
