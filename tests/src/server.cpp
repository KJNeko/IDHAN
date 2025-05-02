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
	SERVER_HANDLE;

	SUCCEED();
}