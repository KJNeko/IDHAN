#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "IDHANTypes.hpp"

namespace idhan::hamming
{
struct HammingPair
{
	RecordID left_id;
	RecordID right_id;
	SmallInt distance;

	auto operator<=>( const HammingPair& ) const = default;
};

class PhashIndex
{
	std::vector< RecordID > m_records {};
	std::vector< std::uint64_t > m_hashes {};

  public:

	PhashIndex() = default;

	//! Both vectors must be the same length and sorted by record ID.
	PhashIndex( std::vector< RecordID > records, std::vector< std::uint64_t > hashes );

	[[nodiscard]] std::size_t size() const noexcept;
	[[nodiscard]] std::optional< std::uint64_t > find( RecordID record ) const;

	//! An empty hash removes the record.
	void assign( RecordID record, std::optional< std::uint64_t > hash );

	//! Sorted, de-duplicated pairs with left_id < right_id.
	[[nodiscard]] std::vector< HammingPair > pairsWithin(
		std::span< const RecordID > probes,
		std::uint8_t max_distance ) const;
};
} // namespace idhan::hamming
