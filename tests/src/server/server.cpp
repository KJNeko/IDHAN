#include <QCoreApplication>

#include <gtest/gtest.h>

#include "NET_CONSTANTS.hpp"
#include "idhan/IDHANClient.hpp"
#include "helpers/serverStarterHelper.hpp"

TEST( ServerTests, ServerSetup )
{
	SERVER_HANDLE;

	SUCCEED();
}