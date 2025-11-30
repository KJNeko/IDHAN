//
// Created by kj16609 on 11/19/24.
//
#pragma once
#include "crypto/SHA256.hpp"
#include "IDHANTypes.hpp"
#include "drogon/orm/DbClient.h"

namespace idhan
{

ExpectedTask< SHA256 > getRecordSHA256( RecordID id, DbClientPtr db = drogon::app().getFastDbClient() );

}
