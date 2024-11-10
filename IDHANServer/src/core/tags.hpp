//
// Created by kj16609 on 9/8/24.
//

#pragma once
#include <string>
#include <utility>

namespace idhan::tags
{

constexpr char NAMESPACE_DELIMTER { ':' };

std::pair< std::string, std::string > split( std::string_view str );

} // namespace idhan::tags
