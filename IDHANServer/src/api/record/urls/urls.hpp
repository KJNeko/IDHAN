//
// Created by kj16609 on 7/24/25.
//
#pragma once
#include <expected>

#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "db/dbTypes.hpp"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"

namespace idhan::helpers
{
ExpectedTask< std::vector< std::string > > fetchUrlsStrings( RecordID record_id, DbClientPtr db );

ExpectedTask< Json::Value > fetchUrlsJson( RecordID record_id, DbClientPtr db );
} // namespace idhan::helpers

namespace idhan
{
using namespace helpers;
}
