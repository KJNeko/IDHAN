#pragma once

#include <string>
#include <string_view>

namespace idhan::db
{
// Display-only whitespace compaction. The result must not be used to execute SQL.
[[nodiscard]] std::string formatSqlForLog( std::string_view sql );
} // namespace idhan::db
