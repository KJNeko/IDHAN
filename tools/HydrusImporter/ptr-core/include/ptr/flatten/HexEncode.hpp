#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace idhan::hydrus::ptr
{

//! Lowercase hex, two characters per byte.
std::string toHex( std::span< const std::byte > bytes );

} // namespace idhan::hydrus::ptr
