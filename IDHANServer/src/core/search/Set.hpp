#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "IDHANTypes.hpp"
#include "SortKey.hpp"

namespace idhan::search
{

class Set
{
	std::vector< RecordID > m_ids {};
	SortKeyColumn m_keys {};
	std::optional< std::vector< SHA256 > > m_hashes {};
	bool m_inverted { false };

	enum class MergeOp
	{
		Intersect,
		Union,
		Difference,
		SymmetricDifference
	};

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

	void reverse();

	void shuffle();

	void slice( std::size_t offset, std::optional< std::size_t > limit );
};

} // namespace idhan::search
