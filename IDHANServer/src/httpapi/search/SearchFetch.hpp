#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "core/search/SearchStats.hpp"
#include "core/search/Set.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::httpapi::searchfetch
{

struct FetchContext
{
	db::DBContext& db;
	SortType sort_type { SortType::DEFAULT };
	bool want_hashes { false };
	std::vector< TagDomainID > tag_domains {};
	std::shared_ptr< idhan::search::SearchStats > stats {};
	HydrusDisplayType display { HydrusDisplayType::DEFAULT };
	bool include_deleted { false };
};

struct PredicateSource
{
	std::string joins {};
	std::string where {};
	std::string description {};
};

[[nodiscard]] std::string_view tagMappingTable( HydrusDisplayType display );

runtime::Task< idhan::search::Set > fetchTag( FetchContext ctx, TagID tag, std::string label );

runtime::Task< idhan::search::Set > fetchAnyTag( FetchContext ctx, std::vector< TagID > tags, std::string label );

runtime::Task< idhan::search::Set > fetchNamespace( FetchContext ctx, NamespaceID tag_namespace, std::string label );

runtime::Task< idhan::search::Set > fetchPredicate( FetchContext ctx, PredicateSource predicate, std::string label );

runtime::Task< idhan::search::Set > fetchPage(
	FetchContext ctx,
	std::vector< RecordID > excluded,
	SortOrder order,
	std::size_t offset,
	std::optional< std::size_t > limit );

[[nodiscard]] std::string buildPageQuery(
	SortType sort_type,
	bool want_hashes,
	bool has_exclusions,
	SortOrder order,
	std::size_t offset,
	std::optional< std::size_t > limit,
	HydrusDisplayType display = HydrusDisplayType::DEFAULT,
	bool include_deleted = false );

} // namespace idhan::httpapi::searchfetch
