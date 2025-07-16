#pragma once
#include "IDHANTypes.hpp"


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"
#pragma GCC diagnostic pop

namespace idhan
{
class SHA256;
}

namespace idhan::api::helpers
{

/**
 * @brief
 * @param sha256s
 * @param db
 * @note Hashes MUST be unique
 * @note Hashes count cannot exceed 100, But can include 100
 * @return
 */
drogon::Task< std::vector< RecordID > >
	massCreateRecord( const std::vector< SHA256 >& sha256s, drogon::orm::DbClientPtr db );

drogon::Task< RecordID > createRecord( const SHA256& sha256, drogon::orm::DbClientPtr db );

drogon::Task< std::optional< RecordID > > searchRecord( const SHA256& sha256, drogon::orm::DbClientPtr db );

} // namespace idhan::api::helpers
