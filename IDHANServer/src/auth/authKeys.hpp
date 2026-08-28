#pragma once

#include <cstddef>

#include "crypto/SHA256.hpp"
#include "db/dbTypes.hpp"
#include "drogon/utils/coroutine.h"

namespace idhan::auth
{

[[nodiscard]] drogon::Task< bool > keyExists( const SHA256& key_hash, DbClientPtr db );

[[nodiscard]] drogon::Task< std::size_t > keyCount( DbClientPtr db );

} // namespace idhan::auth
