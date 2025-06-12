//
// Created by kj16609 on 6/12/25.
//
#pragma once
#include <drogon/drogon.h>

#include <expected>

#include "IDHANTypes.hpp"

namespace idhan::api
{

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	tryParseRecordMetadata( RecordID record_id, drogon::orm::DbClientPtr db );

} // namespace idhan::api
