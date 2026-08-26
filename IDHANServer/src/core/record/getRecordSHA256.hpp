#pragma once
#include <optional>

#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan
{

[[nodiscard]] drogon::Task< std::optional< SHA256 > > findRecordSHA256( RecordID id, DbClientPtr db );

ExpectedTask< SHA256 > getRecordSHA256( RecordID id, DbClientPtr db = drogon::app().getFastDbClient() );

} // namespace idhan
