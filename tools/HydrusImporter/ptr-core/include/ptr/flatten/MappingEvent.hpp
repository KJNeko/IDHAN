#pragma once

#include <cstddef>
#include <cstdint>

namespace idhan::hydrus::ptr
{

//! What a content update did to one (tag, file) pair. Values are load-bearing: Add must be
//! numerically below Delete so that eventLess orders a same-index delete after its add.
enum class EventOp : std::uint8_t
{
	Add = 0,
	Delete = 1
};

#pragma pack( push, 1 )

//! One add-or-delete of one (tag, file) pair, as spilled to a bucket file during the scan.
//! Kept at 12 bytes because the full PTR corpus produces roughly 3.2 billion of these.
struct MappingEvent
{
	std::uint32_t hash_id;
	std::uint32_t tag_id;
	std::uint16_t update_index;
	std::uint8_t op; //!< EventOp
	std::uint8_t pad; //!< Always 0. Keeps the struct at a round 12 bytes.
};

#pragma pack( pop )

static_assert( sizeof( MappingEvent ) == 12, "MappingEvent is written raw to disk; its size is format" );

//! Number of spill buckets. Purely a memory knob: it sets the working-set size of the in-RAM
//! sort in the collapse stage and has no effect on output layout.
inline constexpr std::size_t BUCKET_COUNT { 4096 };

//! Largest update index representable in MappingEvent::update_index. The scan validates against
//! this and aborts rather than truncating.
inline constexpr std::uint16_t MAX_UPDATE_INDEX { 65535 };

//! Every event for a given hash_id lands in exactly one bucket. That is what makes a bucket
//! self-sufficient for collapsing all of its records.
inline constexpr std::size_t bucketFor( const std::uint32_t hash_id )
{
	return hash_id % BUCKET_COUNT;
}

//! Total order (hash_id, tag_id, update_index, op). Sorting a bucket by this makes every
//! (hash_id, tag_id) chain contiguous and chronological, which is what lets collapseChain
//! work over a plain span.
inline bool eventLess( const MappingEvent& a, const MappingEvent& b )
{
	if ( a.hash_id != b.hash_id ) return a.hash_id < b.hash_id;
	if ( a.tag_id != b.tag_id ) return a.tag_id < b.tag_id;
	if ( a.update_index != b.update_index ) return a.update_index < b.update_index;
	return a.op < b.op;
}

} // namespace idhan::hydrus::ptr
