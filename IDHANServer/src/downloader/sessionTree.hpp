#pragma once

#include <drogon/drogon.h>
#include <json/value.h>

#include "IDHANTypes.hpp"

namespace idhan::downloader
{

drogon::Task< Json::Value > sessionUrlTree( drogon::orm::DbClientPtr db, DownloadSessionID session_id, bool flatten );

drogon::Task< Json::Value > sessionSummary( drogon::orm::DbClientPtr db, DownloadSessionID session_id );

} // namespace idhan::downloader
