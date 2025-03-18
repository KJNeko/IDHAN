#pragma once
#include "IDHANTypes.hpp"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"

import sha256;

namespace idhan::api
{

drogon::Task< RecordID > createRecord( const SHA256& sha256, drogon::orm::DbClientPtr db );

drogon::Task< std::optional< RecordID > > searchRecord( const SHA256& sha256, drogon::orm::DbClientPtr db );

} // namespace idhan::api
