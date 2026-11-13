#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace idhan::downloader
{

struct ParserUpdateResult
{
	std::filesystem::path path {};
	// A failed update can leave the previous version active.
	bool active {};
	std::optional< std::string > error {};
};

using ParserUpdateReport = std::vector< ParserUpdateResult >;

} // namespace idhan::downloader
