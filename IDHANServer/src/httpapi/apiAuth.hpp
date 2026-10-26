#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct KeyRejection
{
	std::uint16_t status;
	std::string message;
};

[[nodiscard]] http::HTTPTask< std::optional< KeyRejection > > verifyApiKey( std::string_view key, db::DBContext& db );

[[nodiscard]] http::RequestValidation apiAuthValidation( db::DBContext& db );
[[nodiscard]] http::RouteDefinition verifyRoute( db::DBContext& db );

} // namespace idhan::httpapi
