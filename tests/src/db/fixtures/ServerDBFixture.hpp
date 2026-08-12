#pragma once

#include <gtest/gtest.h>
#include <pqxx/pqxx>

class ServerDBFixture : public testing::Test
{
  protected:

	std::unique_ptr< pqxx::connection > conn;

	void SetUp() override;

	void TearDown() override;
};
