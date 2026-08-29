#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
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
	std::unordered_map< std::string, std::vector< std::uint8_t > > m_entries {};
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

	[[nodiscard]] std::expected< std::span< const std::uint8_t >, std::string > bytecode(
		const std::filesystem::path& script );

	[[nodiscard]] std::expected< std::filesystem::path, std::string > resolve(
		std::string_view specifier,
		const std::filesystem::path& referrer ) const;

	[[nodiscard]] const std::filesystem::path& root() const { return m_root; }
};

} // namespace idhan::downloader
