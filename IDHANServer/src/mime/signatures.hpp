#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "IDHANTypes.hpp"

namespace idhan::mime
{

constexpr std::int64_t SCAN { -1 };

struct Rule
{
	std::int64_t offset { SCAN };
	std::size_t limit { 0 };

	std::span< const std::string_view > patterns {};
};

struct Signature
{
	MimeID mime_id;
	std::span< const Rule > rules;
};

[[nodiscard]] std::span< const Signature > signatures();

} // namespace idhan::mime
