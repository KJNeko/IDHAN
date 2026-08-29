#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "http/LanePolicy.hpp"
#include "scripts/BytecodeCache.hpp"

namespace idhan::downloader
{

struct ScriptRoute
{
	std::string url_class {};
	std::filesystem::path script {};
	std::string export_name {};
};

class ScriptRegistry
{
	struct Route
	{
		std::string export_name {};
		std::optional< std::string > path {};
		std::optional< std::string > path_prefix {};
		std::vector< std::pair< std::string, std::string > > query {};
	};

	struct Host
	{
		std::string host {};
		LaneSettings settings {};
		bool include_subdomains {};
	};

	struct URLClass
	{
		std::string name {};
		std::filesystem::path script {};
		std::vector< Host > hosts {};
		std::vector< Route > routes {};
	};

	std::vector< URLClass > m_classes {};
	std::filesystem::path m_parser_directory {};
	BytecodeCache m_bytecode;
	LaneSettings m_defaults {};

	explicit ScriptRegistry( std::filesystem::path parser_directory, LaneSettings defaults );

  public:

	[[nodiscard]] static std::expected< std::unique_ptr< ScriptRegistry >, std::string > create(
		const std::filesystem::path& url_classes,
		std::filesystem::path parser_directory,
		LaneSettings defaults = {} );

	[[nodiscard]] std::expected< std::optional< ScriptRoute >, std::string > route( std::string_view url ) const;

	[[nodiscard]] LaneSettings laneSettings( std::string_view host ) const;

	[[nodiscard]] BytecodeCache& bytecode() { return m_bytecode; }

	[[nodiscard]] const std::filesystem::path& parserDirectory() const { return m_parser_directory; }
};

} // namespace idhan::downloader
