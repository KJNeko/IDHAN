//
// Created by kj16609 on 8/6/26.
//
// Describes, per SortType, how to obtain the value a Set is ordered by: the joins the expression
// needs, the expression itself, the C++ type it lands in, and whether a NULL value excludes the
// record outright. This table is the only SQL generation left in the search path.
#pragma once

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "SearchTypes.hpp"
#include "crypto/SHA256.hpp"

namespace idhan::search
{

//! Which alternative of SortKeyColumn a sort type produces.
enum class SortKeyType
{
	//! RANDOM: no key at all. Sets order by record_id alone.
	None,
	Integer,
	Real,
	Hash
};

//! The sort key for a whole Set, stored as one column rather than one variant per element -- a
//! per-element variant would spend tag and padding bytes on every record.
using SortKeyColumn =
	std::variant< std::monostate, std::vector< std::int64_t >, std::vector< double >, std::vector< SHA256 > >;

//! Everything the fetch queries need to know about one sort type.
struct SortKeySpec
{
	//! JOIN fragments the expression depends on, beyond the always-present `file_info fi`. Joined
	//! with USING (record_id) so they chain onto whatever relation is driving the query.
	std::string_view joins;
	//! SQL producing the key. Empty for RANDOM, which has no key.
	std::string_view expression;
	SortKeyType type;
	//! When true a NULL key excludes the record, matching the pre-rewrite behaviour where a record
	//! with no resolution data was dropped from a width-sorted search rather than sorted last.
	bool exclude_null;
};

[[nodiscard]] SortKeySpec sortKeySpec( SortType type );

//! An empty column of the alternative \p type selects. Used to seed merge outputs so they hold the
//! right alternative before anything is appended.
[[nodiscard]] SortKeyColumn emptyColumn( SortKeyType type );

//! An empty column holding the same alternative as \p like, without copying its contents.
[[nodiscard]] SortKeyColumn emptyColumnLike( const SortKeyColumn& like );

//! The alternative \p column currently holds.
[[nodiscard]] SortKeyType columnType( const SortKeyColumn& column );

} // namespace idhan::search
