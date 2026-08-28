#pragma once
#include <expected>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan::helpers
{
//! A URL mapped to a record, alongside the domain it was filed under.
struct RecordUrl
{
	std::string url;
	std::string domain;
};

ExpectedTask< std::vector< RecordUrl > > fetchUrlsDetailed( RecordID record_id, DbClientPtr db );

ExpectedTask< std::vector< std::string > > fetchUrlsStrings( RecordID record_id, DbClientPtr db );

ExpectedTask< Json::Value > fetchUrlsJson( RecordID record_id, DbClientPtr db );
} // namespace idhan::helpers

namespace idhan
{
using namespace helpers;
}
