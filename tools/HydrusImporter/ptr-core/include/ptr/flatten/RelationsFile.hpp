#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/FlattenScan.hpp"

namespace idhan::hydrus::ptr
{

inline constexpr const char* RELATIONS_FILENAME { "relations.idhanptr" };

//! One relationship after its chain has been reduced.
struct CollapsedRelation
{
	std::uint32_t a;
	std::uint32_t b;
	EventOp op;
};

//! Reduces every (a, b) pair's history by the same rule collapseChain applies to mappings.
//! \p events may be in any order; it is sorted internally.
//! \return Collapsed relations, sorted by (a, b).
std::vector< CollapsedRelation > collapseRelations( std::vector< RelationEvent > events );

//! Counts from writing a relations file.
struct RelationsFileStats
{
	std::uint64_t parents { 0 };
	std::uint64_t siblings { 0 };
	std::uint64_t missing_definitions { 0 }; //!< pairs dropped because a tag had no definition
};

//! One relationship as read back, with indices into RelationsFile::strings.
struct RelationRecord
{
	std::uint32_t a_index { 0 };
	std::uint32_t b_index { 0 };
	EventOp op { EventOp::Add };
};

//! The relations file, decompressed.
struct RelationsFile
{
	std::vector< ChunkStringEntry > strings {};
	std::vector< RelationRecord > parents {};
	std::vector< RelationRecord > siblings {};
};

//! Writes both relationship kinds into one file with a shared string table. A pair either of
//! whose tags has no definition is dropped and counted; a half-resolved relationship is meaningless.
//!
//! \param usage Marked with every tag id that reaches the string table, or null to track nothing.
//!        A tag surviving only as a parent or sibling endpoint is still created at import, so it
//!        counts as used even though no chunk carries it.
RelationsFileStats writeRelationsFile( const std::filesystem::path& path,
                                       const std::vector< CollapsedRelation >& parents,
                                       const std::vector< CollapsedRelation >& siblings,
	const TagLookup& lookup,
	TagUsageSet* usage = nullptr );

//! \throws std::runtime_error on bad magic, unknown version, or truncation.
RelationsFile readRelationsFile( const std::filesystem::path& path );

} // namespace idhan::hydrus::ptr
