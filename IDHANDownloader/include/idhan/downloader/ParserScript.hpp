#pragma once

#include <cstdint>
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
	std::uint8_t* m_bytes;
	std::size_t m_size;
	std::string m_filename;
	std::shared_ptr< IDHANJSRuntime > m_ctx;

	FGL_DELETE_COPY( CompiledScript );

  public:

	CompiledScript(
		std::uint8_t* bytes,
		const std::size_t size,
		std::shared_ptr< IDHANJSRuntime > ctx,
		std::string filename = {} ) noexcept :
	  m_bytes( bytes ),
	  m_size( size ),
	  m_filename( std::move( filename ) ),
	  m_ctx( std::move( ctx ) )
	{}

	~CompiledScript()
	{
		if ( m_bytes ) js_free( m_ctx->context(), m_bytes );
	}

	std::shared_ptr< ParserInstance > createInstance();

	[[nodiscard]] std::string filename() const { return m_filename; }

	auto size() const { return m_size; }

	[[nodiscard]] std::span< const std::uint8_t > bytecode() const noexcept { return { m_bytes, m_size }; }
};

class ParserScript
{
	std::string m_filename {};
	std::string m_script {};
	std::shared_ptr< CompiledScript > m_compiled {};

	[[nodiscard]] std::shared_ptr< CompiledScript > compileModule( std::shared_ptr< IDHANJSRuntime > ctx ) const;

	[[nodiscard]] JSReturn eval( const std::shared_ptr< IDHANJSRuntime >& ctx, int eval_type ) const;

  public:

	ParserScript( std::string file_name, std::string script );

	[[nodiscard]] std::shared_ptr< ParserInstance > createInstance() const;
};

std::shared_ptr< ParserScript > readScript( const std::filesystem::path& path );
