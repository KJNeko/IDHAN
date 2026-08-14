#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "IDHANTypes.hpp"
#include "SortKey.hpp"

namespace idhan::search
{

/**
 * @brief A sorted, duplicate-free set of records, ordered by the composite key (sort_key, record_id).
 *
 * The composite is what makes the algebra legal. The set operations need both operands ordered by
 * the same strict-weak comparator; the sort key alone is not a total order, but record_id is unique,
 * so the pair is. And the key is a property of the record rather than of the set, so two Sets
 * necessarily agree on the key of any record they share -- intersecting two size-ordered Sets
 * therefore yields a size-ordered Set.
 *
 * A Set may be @em inverted, meaning it denotes the complement of the ids it holds. The universe is
 * never constructed: negation is carried as a flag and rewritten through De Morgan at every
 * operation, so `A & ~B` is a difference rather than a complement followed by an intersection. Only
 * a final result that is still inverted needs the universe at all, and that becomes a `!= ALL(...)`
 * clause on the query that materialises the page.
 */
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

	//! An empty, non-inverted Set whose key column holds the alternative \p type selects.
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
	//! for DESC -- the fetches always produce ascending order.
	void reverse();

	//! Permutes into a random order. Only meaningful for SortType::RANDOM, which carries no key.
	void shuffle();

	//! Keeps only [offset, offset + limit), dropping everything else. A limit of nullopt keeps
	//! everything from offset onwards.
	void slice( std::size_t offset, std::optional< std::size_t > limit );
};

} // namespace idhan::search
