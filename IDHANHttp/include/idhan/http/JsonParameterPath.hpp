#pragma once

#include <utility>

#include "ParameterRules.hpp"

namespace idhan::http
{

class JsonParameterPath
{
	std::vector< std::string > m_path {};

  public:

	[[nodiscard]] JsonParameterPath field( std::string name ) const
	{
		auto result { *this };
		result.m_path.push_back( std::move( name ) );
		return result;
	}

	[[nodiscard]] JsonParameterPath eachElement() const
	{
		auto result { *this };
		result.m_path.emplace_back( "*" );
		return result;
	}

	[[nodiscard]] JsonParameterObject allowFields( std::vector< std::string > names ) const
	{
		return { .path = m_path, .names = std::move( names ) };
	}
};

[[nodiscard]] inline JsonParameterPath root()
{
	return {};
}

} // namespace idhan::http
