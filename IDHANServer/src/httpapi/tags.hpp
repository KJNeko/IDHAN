#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "core/search/SearchTypes.hpp"
#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct SimilarTag
{
	std::string tag_text;
	double similarity;
	TagID tag_id;
	std::int64_t count;
};

[[nodiscard]] std::vector< http::RouteDefinition > tagRoutes( db::DBContext& db );

[[nodiscard]] http::HTTPTask< std::vector< SimilarTag > > getSimilarTags(
	db::DBContext& db,
	std::string search_value,
	std::size_t limit,
	bool include_unused,
	HydrusDisplayType display = HydrusDisplayType::DISPLAY,
	std::vector< TagDomainID > domains = {} );

} // namespace idhan::httpapi
