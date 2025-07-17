//
// Created by kj16609 on 4/3/25.
//

#include <QCoreApplication>

#include <catch2/catch_all.hpp>

#include <random>

#include "NET_CONSTANTS.hpp"
#include "idhan/IDHANClient.hpp"
#include "serverStarterHelper.hpp"

template < typename T >
void qtWaitFuture( QFuture< T >& future )
{
	while ( !future.isFinished() ) QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
}

constexpr char genRealText( std::uint8_t value )
{
	value = value % 62;

	switch ( value )
	{
		case 0:
			return 'a';
		case 1:
			return 'b';
		case 2:
			return 'c';
		case 3:
			return 'd';
		case 4:
			return 'e';
		case 5:
			return 'f';
		case 6:
			return 'g';
		case 7:
			return 'h';
		case 8:
			return 'i';
		case 9:
			return 'j';
		case 10:
			return 'k';
		case 11:
			return 'l';
		case 12:
			return 'm';
		case 13:
			return 'n';
		case 14:
			return 'o';
		case 15:
			return 'p';
		case 16:
			return 'q';
		case 17:
			return 'r';
		case 18:
			return 's';
		case 19:
			return 't';
		case 20:
			return 'u';
		case 21:
			return 'v';
		case 22:
			return 'w';
		case 23:
			return 'x';
		case 24:
			return 'y';
		case 25:
			return 'z';
		case 26:
			return 'A';
		case 27:
			return 'B';
		case 28:
			return 'C';
		case 29:
			return 'D';
		case 30:
			return 'E';
		case 31:
			return 'F';
		case 32:
			return 'G';
		case 33:
			return 'H';
		case 34:
			return 'I';
		case 35:
			return 'J';
		case 36:
			return 'K';
		case 37:
			return 'L';
		case 38:
			return 'M';
		case 39:
			return 'N';
		case 40:
			return 'O';
		case 41:
			return 'P';
		case 42:
			return 'Q';
		case 43:
			return 'R';
		case 44:
			return 'S';
		case 45:
			return 'T';
		case 46:
			return 'U';
		case 47:
			return 'V';
		case 48:
			return 'W';
		case 49:
			return 'X';
		case 50:
			return 'Y';
		case 51:
			return 'Z';
		case 52:
			return '0';
		case 53:
			return '1';
		case 54:
			return '2';
		case 55:
			return '3';
		case 56:
			return '4';
		case 57:
			return '5';
		case 58:
			return '6';
		case 59:
			return '7';
		case 60:
			return '8';
		case 61:
			return '9';
		default:
			return 'a';
	}
}

std::pair< std::string, std::string > generateTag()
{
	// mt199975
	std::random_device rd {};
	std::mt19937 gen { rd() };
	std::uniform_int_distribution< std::uint8_t > uni { 0, 61 };

	std::string gen_ns, gen_st {};
	gen_ns.resize( 16 );
	gen_st.resize( 36 );

	for ( std::size_t i = 0; i < gen_ns.size(); i++ ) gen_ns[ i ] = genRealText( uni( gen ) );

	for ( std::size_t i = 0; i < gen_st.size(); i++ ) gen_st[ i ] = genRealText( uni( gen ) );

	return { gen_ns, gen_st };
}

TEST_CASE( "Client tests", "[server][client][network]" )
{
	int argc { 0 };
	QCoreApplication app { argc, nullptr };

	SERVER_HANDLE;

	SECTION( "Client connection" )
	{
		idhan::IDHANClient client { "localhost", idhan::IDHAN_DEFAULT_PORT, false };

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

					SECTION( "Empty namespace" )
					{
						auto tag_future { client.createTag( "", "highschool dxd" ) };
						qtWaitFuture( tag_future );
						REQUIRE( tag_future.resultCount() > 0 );
						REQUIRE( tag_future.resultCount() == 1 );
					}

					SECTION( "Existing tag" )
					{
						auto tag_future { client.createTag( "character", "toujou koneko" ) };
						qtWaitFuture( tag_future );
						REQUIRE( tag_future.resultCount() > 0 );
						REQUIRE( tag_future.resultCount() == 1 );
						const auto tag_id { tag_future.result() };
						idhan::logging::info( "Got tag ID {} for tag {}", "character:toujou koneko", tag_id );
						auto tag_future2 { client.createTag( "character", "toujou koneko" ) };
						qtWaitFuture( tag_future2 );
						REQUIRE( tag_future2.resultCount() > 0 );
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

						const auto tag_ids { future.result() };

						REQUIRE( tag_ids.size() == tags.size() );
					}

					SECTION( "Combined string" )
					{
						const std::vector< std::string > tags { "character:toujou koneko", "series:highschool dxd" };

						auto future { client.createTags( tags ) };

						qtWaitFuture( future );

						REQUIRE( future.resultCount() > 0 );

						const auto tag_ids { future.result() };

						REQUIRE( tag_ids.size() == tags.size() );
					}
				}
			}
		}
	}

	SUCCEED();
}

/*
TEST_CASE( "Benchmarks" )
{
	int argc { 0 };
	QCoreApplication app { argc, nullptr };

	SERVER_HANDLE;

	idhan::IDHANClientConfig config {};
	config.hostname = "localhost";
	config.port = idhan::IDHAN_DEFAULT_PORT;
	config.self_name = "testing suite";
	config.use_ssl = false;

	idhan::IDHANClient client { config };

	BENCHMARK_ADVANCED( "Create tags" )( Catch::Benchmark::Chronometer meter )
	{
		std::vector< std::pair< std::string, std::string > > tags( 16 );

		for ( std::size_t i = 0; i < tags.size(); i++ ) tags[ i ] = generateTag();

		meter.measure(
			[ & ]
			{
				auto future { client.createTags( tags ) };
				qtWaitFuture( future );
				return future.result();
			} );
	};
}
*/