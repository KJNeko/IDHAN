//
// Created by kj16609 on 4/3/25.
//

#include <QCoreApplication>

#include <catch2/catch_all.hpp>

#include "NET_CONSTANTS.hpp"
#include "idhan/IDHANClient.hpp"
#include "serverStarterHelper.hpp"

template < typename T >
void qtWaitFuture( QFuture< T >& future )
{
	while ( !future.isFinished() ) QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
}

TEST_CASE( "Client tests", "[server][client][network]" )
{
	int argc { 0 };
	QCoreApplication app { argc, nullptr };

	SERVER_HANDLE;

	SECTION( "Client connection" )
	{
		idhan::IDHANClientConfig config {};
		config.hostname = "localhost";
		config.port = idhan::IDHAN_DEFAULT_PORT;
		config.self_name = "testing suite";
		config.use_ssl = false;

		// volatile prevents it being optimized away
		idhan::IDHANClient client { config };

		SECTION( "Function tests" )
		{
			SECTION( "Tags" )
			{
				SECTION( "IDHANClient::createTag" )
				{
					SECTION( "Single string" )
					{
						auto tag_future { client.createTag( "character:toujou koneko" ) };

						qtWaitFuture( tag_future );

						REQUIRE( tag_future.resultCount() > 0 );
						REQUIRE( tag_future.resultCount() == 1 );

						const auto tag_id { tag_future.result() };
						idhan::logging::info( "Got tag ID {} for tag {}", "character:toujou koneko", tag_id );
					}

					SECTION( "Split string" )
					{
						auto tag_future { client.createTag( "series", "highschool dxd" ) };

						qtWaitFuture( tag_future );

						REQUIRE( tag_future.resultCount() > 0 );
						REQUIRE( tag_future.resultCount() == 1 );

						const auto tag_id { tag_future.result() };
						idhan::logging::info( "Got tag ID {} for tag {}", "series:highschool dxd", tag_id );
					}
				}

				SECTION( "IDHANClient::createTags" )
				{
					SECTION( "Split strings" )
					{
						const std::vector< std::pair< std::string, std::string > > tags {
							{ "character", "toujou koneko" }, { "series", "highschool dxd" }
						};

						auto future { client.createTags( tags ) };

						qtWaitFuture( future );

						REQUIRE( future.resultCount() > 0 );
						REQUIRE( future.resultCount() == 2 );
					}
				}
			}
		}
	}

	SUCCEED();
}