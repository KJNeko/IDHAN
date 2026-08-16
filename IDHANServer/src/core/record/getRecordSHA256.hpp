#pragma once
#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/orm/DbClient.h"

namespace idhan
{

ExpectedTask< SHA256 > getRecordSHA256( RecordID id, DbClientPtr db = drogon::app().getFastDbClient() );

}
