#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

#include "ptr/PTRFileParser.hpp"
#include "ptr/flatten/MappingEvent.hpp"

namespace idhan::hydrus::ptr
{

#pragma pack( push, 1 )

//! An add-or-delete of one tag relationship. For parents (a, b) is (child, parent); for siblings
//! it is (bad, good). Held in memory rather than spilled: the whole corpus has roughly 186,000.
struct RelationEvent
{
	std::uint32_t a;
	std::uint32_t b;
	std::uint16_t update_index;
	std::uint8_t op; //!< EventOp
	std::uint8_t pad;
};

#pragma pack( pop )

static_assert( sizeof( RelationEvent ) == 12 );

//! Host hooks. All may be empty; scanCorpus checks before calling.
struct ScanCallbacks
{
	//! Polled once per update file. Return true to stop early.
	std::function< bool() > cancelled {};
	//! (files done, files total, current status text)
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress {};
	//! Fired once per update file, after it is processed: (events written to buckets so far,
	//! update files skipped so far).
	std::function< void( std::uint64_t, std::uint64_t ) > statsUpdated {};
};

//! What the scan produced. Mapping events are on disk in the work directory's buckets;
//! relationships and the counters come back here.
struct ScanResult
{
	std::int32_t first_update_index { 0 };
	std::int32_t last_update_index { 0 };
	std::uint64_t events_written { 0 };
	std::uint64_t skipped_files { 0 }; //!< missing or unparseable
	std::uint64_t rejected_hashes { 0 };
	std::vector< RelationEvent > parents {};
	std::vector< RelationEvent > siblings {};
	bool cancelled { false };
};

//! Reads every update file listed in \p metadata, in ascending update index, writing definitions
//! into \p work_dir's definition store and mapping events into its buckets.
//!
//! A file that is missing or fails to parse is logged, counted in skipped_files, and stepped over:
//! one bad file must not abort a multi-hour run.
//!
//! \throws std::runtime_error if an update index exceeds MAX_UPDATE_INDEX. Truncating it would
//!         silently corrupt chain ordering, so this fails loudly instead.
ScanResult scanCorpus( const std::filesystem::path& ptr_dir,
                       const MetadataUpdate& metadata,
                       const std::filesystem::path& work_dir,
                       const ScanCallbacks& callbacks );

} // namespace idhan::hydrus::ptr
