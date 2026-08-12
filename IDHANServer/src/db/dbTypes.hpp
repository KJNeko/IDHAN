#pragma once
#include "drogon/orm/DbClient.h"

namespace idhan
{
//! Alias for Drogon's async database client pointer, used throughout the server's coroutine DB access.
using DbClientPtr = drogon::orm::DbClientPtr;
} // namespace idhan
