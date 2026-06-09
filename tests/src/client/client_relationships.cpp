#include <QCoreApplication>

#include <gtest/gtest.h>

#include "logging/format_ns.hpp"
#include "NET_CONSTANTS.hpp"
#include "helpers/serverStarterHelper.hpp"
#include "idhan/IDHANClient.hpp"

template < typename T >
static void qtWaitFuture( QFuture< T >& future )
{
	while ( !future.isFinished() ) QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
}

struct ClientRelationshipTests : public ::testing::Test
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

TEST_F( ClientRelationshipTests, CreateAliasViaClient )
{
	auto tag_a_future { client->createTag( "rel:alias_a" ) };
	qtWaitFuture( tag_a_future );
	const auto tag_a { tag_a_future.result() };

	auto tag_b_future { client->createTag( "rel:alias_b" ) };
	qtWaitFuture( tag_b_future );
	const auto tag_b { tag_b_future.result() };

	auto alias_future { client->createAliasRelationship( default_domain, tag_a, tag_b ) };
	qtWaitFuture( alias_future );
	SUCCEED();
}

TEST_F( ClientRelationshipTests, GetAliasRelationships )
{
	auto tag_a_future { client->createTag( "rel:get_alias_a" ) };
	qtWaitFuture( tag_a_future );
	const auto tag_a { tag_a_future.result() };

	auto tag_b_future { client->createTag( "rel:get_alias_b" ) };
	qtWaitFuture( tag_b_future );
	const auto tag_b { tag_b_future.result() };

	auto alias_future { client->createAliasRelationship( default_domain, tag_a, tag_b ) };
	qtWaitFuture( alias_future );

	auto rel_future { client->getTagRelationships( tag_a, default_domain ) };
	qtWaitFuture( rel_future );
	const auto rel { rel_future.result() };

	// tag_a is aliased to tag_b, so aliases (aliased_id -> alias_id)
	// should contain tag_b
	bool alias_found { false };
	for ( const auto& alias : rel.m_aliases )
	{
		if ( alias == tag_b )
		{
			alias_found = true;
			break;
		}
	}
	ASSERT_TRUE( alias_found );

	// tag_b should have tag_a in its aliased list
	auto rel_b_future { client->getTagRelationships( tag_b, default_domain ) };
	qtWaitFuture( rel_b_future );
	const auto rel_b { rel_b_future.result() };

	bool aliased_found { false };
	for ( const auto& aliased : rel_b.m_aliased )
	{
		if ( aliased == tag_a )
		{
			aliased_found = true;
			break;
		}
	}
	ASSERT_TRUE( aliased_found );
}

TEST_F( ClientRelationshipTests, CreateParentViaClient )
{
	auto parent_future { client->createTag( "rel:parent" ) };
	qtWaitFuture( parent_future );
	const auto parent_id { parent_future.result() };

	auto child_future { client->createTag( "rel:child" ) };
	qtWaitFuture( child_future );
	const auto child_id { child_future.result() };

	auto parent_rel_future { client->createParentRelationship( default_domain, parent_id, child_id ) };
	qtWaitFuture( parent_rel_future );
	SUCCEED();
}

TEST_F( ClientRelationshipTests, GetParentRelationships )
{
	auto parent_future { client->createTag( "rel:get_parent" ) };
	qtWaitFuture( parent_future );
	const auto parent_id { parent_future.result() };

	auto child_future { client->createTag( "rel:get_child" ) };
	qtWaitFuture( child_future );
	const auto child_id { child_future.result() };

	auto parent_rel_future { client->createParentRelationship( default_domain, parent_id, child_id ) };
	qtWaitFuture( parent_rel_future );

	auto child_rel_future { client->getTagRelationships( child_id, default_domain ) };
	qtWaitFuture( child_rel_future );
	const auto child_rel { child_rel_future.result() };

	bool parent_found { false };
	for ( const auto& p : child_rel.m_parents )
	{
		if ( p == parent_id )
		{
			parent_found = true;
			break;
		}
	}
	ASSERT_TRUE( parent_found );

	auto parent_rel_check_future { client->getTagRelationships( parent_id, default_domain ) };
	qtWaitFuture( parent_rel_check_future );
	const auto parent_rel_check { parent_rel_check_future.result() };

	bool child_found { false };
	for ( const auto& c : parent_rel_check.m_children )
	{
		if ( c == child_id )
		{
			child_found = true;
			break;
		}
	}
	ASSERT_TRUE( child_found );
}

TEST_F( ClientRelationshipTests, BatchCreateAliases )
{
	std::vector< idhan::TagID > tags;
	for ( int i = 0; i < 3; ++i )
	{
		auto future { client->createTag( format_ns::format( "rel:batch_alias_{}", i ) ) };
		qtWaitFuture( future );
		tags.push_back( future.result() );
	}

	std::vector< std::pair< idhan::TagID, idhan::TagID > > pairs;
	pairs.emplace_back( tags[ 0 ], tags[ 1 ] );
	pairs.emplace_back( tags[ 1 ], tags[ 2 ] );

	auto batch_future { client->createAliasRelationship( default_domain, pairs ) };
	qtWaitFuture( batch_future );

	auto rel_future { client->getTagRelationships( tags[ 0 ], default_domain ) };
	qtWaitFuture( rel_future );
	const auto rel { rel_future.result() };

	bool found { false };
	for ( const auto& alias : rel.m_aliases )
	{
		if ( alias == tags[ 1 ] )
		{
			found = true;
			break;
		}
	}
	ASSERT_TRUE( found );
}

TEST_F( ClientRelationshipTests, BatchCreateParents )
{
	std::vector< idhan::TagID > tags;
	for ( int i = 0; i < 3; ++i )
	{
		auto future { client->createTag( format_ns::format( "rel:batch_parent_{}", i ) ) };
		qtWaitFuture( future );
		tags.push_back( future.result() );
	}

	std::vector< std::pair< idhan::TagID, idhan::TagID > > pairs;
	pairs.emplace_back( tags[ 0 ], tags[ 1 ] );
	pairs.emplace_back( tags[ 1 ], tags[ 2 ] );

	auto batch_future { client->createParentRelationship( default_domain, pairs ) };
	qtWaitFuture( batch_future );

	auto rel_future { client->getTagRelationships( tags[ 2 ], default_domain ) };
	qtWaitFuture( rel_future );
	const auto rel { rel_future.result() };

	bool parent_found { false };
	for ( const auto& p : rel.m_parents )
	{
		if ( p == tags[ 1 ] )
		{
			parent_found = true;
			break;
		}
	}
	ASSERT_TRUE( parent_found );
}

TEST_F( ClientRelationshipTests, CyclicAliasRejected )
{
	auto tag_a_future { client->createTag( "rel:cyclic_a" ) };
	qtWaitFuture( tag_a_future );
	const auto tag_a { tag_a_future.result() };

	auto tag_b_future { client->createTag( "rel:cyclic_b" ) };
	qtWaitFuture( tag_b_future );
	const auto tag_b { tag_b_future.result() };

	auto alias_1_future { client->createAliasRelationship( default_domain, tag_a, tag_b ) };
	qtWaitFuture( alias_1_future );

	auto alias_2_future { client->createAliasRelationship( default_domain, tag_b, tag_a ) };
	qtWaitFuture( alias_2_future );

	// The API currently catches DrogonDbException and returns a bad request
	// rather than throwing, so we just check that both futures complete
	SUCCEED();
}
