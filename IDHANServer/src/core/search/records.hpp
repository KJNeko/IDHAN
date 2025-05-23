//
// Created by kj16609 on 11/19/24.
//
#pragma once
#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/orm/DbClient.h"

namespace idhan
{

SHA256 getRecordSHA256( const RecordID id, drogon::orm::DbClientPtr db );

}