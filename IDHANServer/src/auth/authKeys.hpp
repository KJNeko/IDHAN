#pragma once

#include <cstddef>

#include "idhan/SHA256.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::auth
{

[[nodiscard]] runtime::Task< bool > keyExists( const SHA256& key_hash, db::DBContext& db );
[[nodiscard]] runtime::Task< std::size_t > keyCount( db::DBContext& db );

} // namespace idhan::auth
