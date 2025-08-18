//
// Created by kj16609 on 8/18/25.
//
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
