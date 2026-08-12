#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace idhan::hydrus::ptr
{

//! Which PTR tag ids actually reached an output file, as one bit per id.
//!
//! The PTR defines far more tags than survive a collapse. Subtracting this from the definition
//! store's defined-tag count is what "tags disregarded because they were unused" means: defined in
//! the corpus, but never written to a chunk or the relations file, and therefore never created in
//! IDHAN.
//!
//! Sized from the definition store's id capacity, so marking is an array index rather than a hash
//! lookup and an unused id range costs a bit each rather than a node. At PTR scale the whole set is
//! a few megabytes.
//!
//! mark() is thread-safe and idempotent. Chunks are sealed concurrently and one tag's text is
//! carried by hundreds of them, so both properties are load-bearing: the marks race by design and
//! must still total the number of distinct ids.
class TagUsageSet
{
  public:

	//! \param capacity One past the highest markable id. Usually DefinitionReader::tagIdCapacity().
	explicit TagUsageSet( std::uint32_t capacity );

	TagUsageSet( const TagUsageSet& ) = delete;
	TagUsageSet& operator=( const TagUsageSet& ) = delete;
	TagUsageSet( TagUsageSet&& ) = delete;
	TagUsageSet& operator=( TagUsageSet&& ) = delete;

	~TagUsageSet() = default;

	//! Ids at or past the capacity are ignored: an id with no slot in the definition store has no
	//! text, so it cannot have been written to an output file in the first place.
	void mark( std::uint32_t tag_id ) noexcept;

	//! \pre Every marking thread has joined.
	std::uint64_t count() const noexcept;

	std::uint32_t capacity() const noexcept { return m_capacity; }

  private:

	//! Bits are packed 64 to a word, so the relaxed fetch_or below is one uncontended cache line
	//! per 64 consecutive ids rather than one atomic per id.
	static constexpr std::uint32_t BITS_PER_WORD { 64 };

	std::uint32_t m_capacity;
	std::vector< std::atomic< std::uint64_t > > m_words;
};

} // namespace idhan::hydrus::ptr
