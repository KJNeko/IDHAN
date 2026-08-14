#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "SearchStats.hpp"
#include "Set.hpp"
#include "db/dbTypes.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::search
{

struct FetchContext
{
	DbClientPtr db {};
	SortType sort_type { SortType::DEFAULT };
	//! When set every fetch also carries each record's sha256, so materialising a page needs no
	//! follow-up query.
	bool want_hashes { false };
	//! Empty means every domain; non-empty is bound to $1 and filters the mapping lookups.
	std::vector< TagDomainID > tag_domains {};
	//! Where each fetch reports its row count and duration. May be null, in which case nothing is
	//! recorded. Shared rather than owned because every concurrent fetch writes to the same one.
	std::shared_ptr< SearchStats > stats {};
};

//! A system predicate reduced to the SQL that enumerates the records satisfying it.
struct PredicateSource
{
	//! Extra joins. Aliases are `p`-prefixed so they can never collide with the sort key's, which
	//! matters when a predicate and the sort read the same table (`system:no duration` under a
	//! duration sort, say).
	std::string joins {};
	//! Boolean expression over file_info (aliased `fi`) and whatever `joins` brings in. Never empty.
	std::string where {};
	//! How the predicate reads back to whoever typed it, e.g. `system:width >= 500`.
	std::string description {};
};

Task< Set > fetchTag( FetchContext ctx, TagID tag, std::string label );

Task< Set > fetchAnyTag( FetchContext ctx, std::vector< TagID > tags, std::string label );

Task< Set > fetchNamespace( FetchContext ctx, NamespaceID tag_namespace, std::string label );

Task< Set > fetchPredicate( FetchContext ctx, PredicateSource predicate, std::string label );

//! Direct DB page for unfiltered browse and pure-exclusion searches; avoids materialising all records.
Task< Set > fetchPage(
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
	std::optional< std::size_t > limit );

} // namespace idhan::search
