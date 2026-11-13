#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct LayoutSummaryResponse
{
	std::string id;
	std::string name;
	std::int32_t schema;
	std::int64_t created_at;
	std::int64_t updated_at;
};

struct LayoutIdResponse
{
	std::string id;
};

struct DeletedLayoutResponse
{
	bool deleted;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/layouts"}]]
[[= http::Group{"Layouts"}]]
http::HTTPTask< http::JsonResult< std::vector< LayoutSummaryResponse > > > listLayouts( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/layouts"}]]
[[= http::Group{"Layouts"}]]
[[= http::Input<>{}]]
http::HTTPTask< http::JsonResult< LayoutIdResponse, 201 > > createLayout( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/layouts/{id}"}]]
[[= http::Group{"Layouts"}]]
[[= http::Validation< http::validation::UUID >{"id"}]]
http::HTTPTask< http::JsonResult< http::JsonDocument > > getLayout( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::PUT, "/layouts/{id}"}]]
[[= http::Group{"Layouts"}]]
[[= http::Validation< http::validation::UUID >{"id"}]]
[[= http::Input<>{}]]
http::HTTPTask< http::JsonResult< LayoutIdResponse > > putLayout( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/layouts/{id}"}]]
[[= http::Group{"Layouts"}]]
[[= http::Validation< http::validation::UUID >{"id"}]]
http::HTTPTask< http::JsonResult< DeletedLayoutResponse > > deleteLayout( db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
