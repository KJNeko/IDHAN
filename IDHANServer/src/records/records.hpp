#pragma once
#include <expected>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"

namespace idhan
{
class SHA256;
}

namespace idhan::helpers

{
/**
 * @brief
 * @param sha256s
 * @param db
 * @note Hashes MUST be unique
 * @return
 */
drogon::Task< std::vector< RecordID > > massCreateRecord( const std::vector< SHA256 >& sha256s, DbClientPtr db );

drogon::Task< std::expected< RecordID, drogon::HttpResponsePtr > > createRecord( const SHA256& sha256, DbClientPtr db );

drogon::Task< std::optional< RecordID > > findRecord( const SHA256& sha256, DbClientPtr db );

} // namespace idhan::helpers
