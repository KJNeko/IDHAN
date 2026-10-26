#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "SearchTypes.hpp"
#include "idhan/SHA256.hpp"

namespace idhan::search
{

enum class SortKeyType
{
	None,
	Integer,
	Real,
	Hash
};

using SortKeyColumn =
	std::variant< std::monostate, std::vector< std::int64_t >, std::vector< double >, std::vector< SHA256 > >;

struct SortKeySpec
{
	std::string_view joins;
	std::string_view expression;
	SortKeyType type;
	bool exclude_null;
};

[[nodiscard]] SortKeySpec sortKeySpec( SortType type, HydrusDisplayType display = HydrusDisplayType::DEFAULT );

[[nodiscard]] std::string_view sortTypeName( SortType type );

[[nodiscard]] std::string describeSortKey( SortType type, HydrusDisplayType display = HydrusDisplayType::DEFAULT );

[[nodiscard]] SortKeyColumn emptyColumn( SortKeyType type );

[[nodiscard]] SortKeyColumn emptyColumnLike( const SortKeyColumn& like );

[[nodiscard]] SortKeyType columnType( const SortKeyColumn& column );

} // namespace idhan::search
