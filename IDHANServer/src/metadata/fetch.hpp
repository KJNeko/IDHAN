//
// Created by kj16609 on 11/13/25.
//
#pragma once
#include <expected>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan::metadata
{

ExpectedTask< void > addFileSpecificInfo( Json::Value& root, RecordID record_id, DbClientPtr db );

}