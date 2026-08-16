#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "IDHANTypes.hpp"
#include "SortKey.hpp"

namespace idhan::search
{

//! Sorted by (sort_key, record_id), so set algebra preserves result ordering. Inverted sets carry
//! complement semantics without materialising the universe until page fetch.
class Set
{
	//! Sorted by (key, record_id); unique.
	std::vector< RecordID > m_ids {};
	//! Parallel to m_ids. Holds std::monostate for RANDOM, which has no key.
	SortKeyColumn m_keys {};
	//! Parallel to m_ids when present. Absent unless the caller asked for hashes.
	std::optional< std::vector< SHA256 > > m_hashes {};
	//! When true this Set denotes the complement of m_ids.
	bool m_inverted { false };

	enum class MergeOp
	{
		Intersect,
		Union,
		//! Everything in the left operand that is not in the right.
		Difference,
		SymmetricDifference
	};

	//! The single linear merge every operation delegates to. Emits ids and every payload column
	//! together, so the parallel arrays cannot drift.
	[[nodiscard]] static Set merge( const Set& lhs, const Set& rhs, MergeOp op, bool inverted );

  public:

	Set() = default;

	Set( std::vector< RecordID > ids,
	     SortKeyColumn keys,
	     std::optional< std::vector< SHA256 > > hashes,
	     bool inverted = false );

	[[nodiscard]] static Set emptyOf( SortKeyType type );

	[[nodiscard]] const std::vector< RecordID >& ids() const noexcept { return m_ids; }

	[[nodiscard]] const std::optional< std::vector< SHA256 > >& hashes() const noexcept { return m_hashes; }

	[[nodiscard]] bool inverted() const noexcept { return m_inverted; }

	[[nodiscard]] std::size_t size() const noexcept { return m_ids.size(); }

	[[nodiscard]] bool empty() const noexcept { return m_ids.empty(); }

	[[nodiscard]] SortKeyType keyType() const noexcept { return columnType( m_keys ); }

	[[nodiscard]] Set operator&( const Set& other ) const;
	[[nodiscard]] Set operator|( const Set& other ) const;
	[[nodiscard]] Set operator^( const Set& other ) const;
	[[nodiscard]] Set operator~() const &;
	[[nodiscard]] Set operator~() &&;

	[[nodiscard]] Set intersect( const Set& other ) const { return *this & other; }

	[[nodiscard]] Set unite( const Set& other ) const { return *this | other; }

	[[nodiscard]] Set symmetricDifference( const Set& other ) const { return *this ^ other; }

	[[nodiscard]] Set negate() const { return ~*this; }

	//! Flips into descending composite order. Applied once, after the algebra, when the search asks
	//! for DESC, since the fetches always produce ascending order.
	void reverse();

	//! Permutes into a random order. Only meaningful for SortType::RANDOM, which carries no key.
	void shuffle();

	//! Keeps only [offset, offset + limit), dropping everything else. A limit of nullopt keeps
	//! everything from offset onwards.
	void slice( std::size_t offset, std::optional< std::size_t > limit );
};

} // namespace idhan::search
