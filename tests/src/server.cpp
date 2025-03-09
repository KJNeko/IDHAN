//
// Created by kj16609 on 2/21/24.
//

#include <QCoreApplication>

#include <catch2/catch_all.hpp>

#include "NET_CONSTANTS.hpp"
#include "idhan/IDHANClient.hpp"
#include "serverStarterHelper.hpp"

TEST_CASE( "Server setup", "[server][network]" )
{
	const auto _ { startServer() };

	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

	SECTION( "Connect to the server" )
	{
		idhan::IDHANClientConfig config {};
		config.hostname = "localhost";
		config.port = idhan::IDHAN_DEFAULT_PORT;
		config.self_name = "testing suite";
		config.use_ssl = false;

		int argc { 0 };

		QCoreApplication app { argc, nullptr };

		idhan::IDHANClient client { config };

		std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
	}

	SUCCEED();
}