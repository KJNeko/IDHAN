#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "JSReturn.hpp"

class ParserInstance;

class CompiledScript : public std::enable_shared_from_this< CompiledScript >
{
	std::vector< std::uint8_t > m_bytes {};
	std::string m_filename;

	FGL_DELETE_COPY( CompiledScript );

  public:

	CompiledScript( const std::uint8_t* bytes, const std::size_t size, std::string filename = {} ) noexcept :
	  m_bytes( size ),
	  m_filename( std::move( filename ) )
	{
		std::memcpy( m_bytes.data(), bytes, size );
	}

	~CompiledScript() = default;

	std::shared_ptr< ParserInstance > createInstance();

	[[nodiscard]] std::string filename() const { return m_filename; }

	auto size() const { return m_bytes.size(); }

	[[nodiscard]] std::span< const std::uint8_t > bytecode() const noexcept
	{
		return m_bytes;
		;
	}
};

class ParserScript
{
	std::string m_filename {};
	std::string m_script {};
	std::shared_ptr< CompiledScript > m_compiled {};

	[[nodiscard]] std::shared_ptr< CompiledScript > compileModule( const std::shared_ptr< IDHANJSRuntime >& ctx ) const;

	[[nodiscard]] JSReturn eval( const std::shared_ptr< IDHANJSRuntime >& ctx, int eval_type ) const;

  public:

	ParserScript( std::string file_name, std::string script );

	[[nodiscard]] std::shared_ptr< ParserInstance > createInstance() const;
};

std::shared_ptr< ParserScript > readScript( const std::filesystem::path& path );
