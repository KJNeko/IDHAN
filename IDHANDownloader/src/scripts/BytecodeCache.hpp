#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <mutex>
#include <quickjs.h>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "js/QuickJSHandles.hpp"

namespace idhan::downloader
{

class BytecodeCache
{
	std::filesystem::path m_root;
	mutable std::recursive_mutex m_mutex {};
	std::unordered_map< std::string, std::string > m_sources {};
	std::unordered_map< std::string, std::vector< std::uint8_t > > m_entries {};
	mutable std::map< std::pair< std::filesystem::path, std::string >, std::filesystem::path > m_resolutions {};
	JSRuntimePtr m_runtime {};
	JSContextPtr m_context {};

	[[nodiscard]] std::expected< JSModuleDef*, std::string > compile( const std::filesystem::path& script );

	[[nodiscard]] static std::expected< std::filesystem::path, std::string > locate(
		const std::filesystem::path& target );

	static char* normalizeModule( JSContext* context, const char* base, const char* name, void* opaque );
	static JSModuleDef* loadModule( JSContext* context, const char* name, void* opaque );

  public:

	static constexpr std::string_view packages_directory { "packages" };

	explicit BytecodeCache( std::filesystem::path root );
	BytecodeCache( const BytecodeCache& ) = delete;
	BytecodeCache& operator=( const BytecodeCache& ) = delete;
	~BytecodeCache();

	// Combine already checked module graphs before publishing a new registry version.
	// Conflicting shared dependencies require updating their parsers together.
	[[nodiscard]] std::expected< void, std::string > merge( const BytecodeCache& prepared );

	[[nodiscard]] std::expected< std::span< const std::uint8_t >, std::string > bytecode(
		const std::filesystem::path& script );

	// Runtime lookups never read files or compile modules. Preparation must populate the snapshot first.
	[[nodiscard]] std::expected< std::span< const std::uint8_t >, std::string > preparedBytecode(
		const std::filesystem::path& script ) const;
	[[nodiscard]] std::expected< std::filesystem::path, std::string > resolvePrepared(
		std::string_view specifier,
		const std::filesystem::path& referrer ) const;

	[[nodiscard]] std::expected< std::filesystem::path, std::string > resolve(
		std::string_view specifier,
		const std::filesystem::path& referrer ) const;

	[[nodiscard]] const std::filesystem::path& root() const { return m_root; }
};

} // namespace idhan::downloader
