#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace idhan::hydrus::ptr
{

//! Which PTR tag ids actually reached an output file, as one bit per id.
//! mark() is thread-safe and idempotent.
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

	//! Ids at or past the capacity are ignored.
	void mark( std::uint32_t tag_id ) noexcept;

	//! \pre Every marking thread has joined.
	std::uint64_t count() const noexcept;

	std::uint32_t capacity() const noexcept { return m_capacity; }

  private:

	static constexpr std::uint32_t BITS_PER_WORD { 64 };

	std::uint32_t m_capacity;
	std::vector< std::atomic< std::uint64_t > > m_words;
};

} // namespace idhan::hydrus::ptr
