#include "ParserScript.hpp"

#include <spdlog/spdlog.h>

#include <fstream>
#include <stdexcept>
#include <utility>

#include "ParserInstance.hpp"

std::shared_ptr< ParserInstance > CompiledScript::createInstance()
{
	return std::shared_ptr< ParserInstance >( new ParserInstance( shared_from_this() ) );
}

std::shared_ptr< CompiledScript > ParserScript::compileModule( std::shared_ptr< IDHANJSRuntime > ctx ) const
{
	auto value { this->eval( ctx, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY ) };

	if ( value.isException() ) throw std::runtime_error( "Failed to compile script" );

	std::size_t size { 0 };
	std::uint8_t* bytes { JS_WriteObject( ctx->context(), &size, value.m_value, JS_WRITE_OBJ_BYTECODE ) };

	if ( size <= 0 || bytes == nullptr ) throw std::runtime_error( "Failed to compile script" );

	return std::make_shared< CompiledScript >( bytes, size, ctx, m_filename );
}

ParserScript::ParserScript( std::string file_name, std::string script ) :
  m_filename( std::move( file_name ) ),
  m_script( std::move( script ) )
{
	std::shared_ptr< IDHANJSRuntime > runtime { std::make_shared< IDHANJSRuntime >() };
	m_compiled = this->compileModule( runtime );

	spdlog::info(
		"Loaded script {} with size {} ({} compiled)", this->m_filename, m_script.size(), m_compiled->size() );
}

JSReturn ParserScript::eval( const std::shared_ptr< IDHANJSRuntime >& ctx, int eval_type ) const
{
	const auto result { JS_Eval( ctx->context(), m_script.c_str(), m_script.size(), m_filename.c_str(), eval_type ) };

	JSReturn ret { ctx, result };
	return ret;
}

std::shared_ptr< ParserScript > readScript( const std::filesystem::path& path )
{
	//TODO: Replace with io_uring
	if ( std::ifstream ifs( path.string(), std::ios::binary | std::ios::ate ); ifs )
	{
		std::string out {};
		out.resize( ifs.tellg() );
		ifs.seekg( std::ios::beg );
		ifs.read( out.data(), out.size() );

		return std::make_shared< ParserScript >( path.filename().string(), std::move( out ) );
	}

	spdlog::error( "Failed to load {} at pwd {}", path.string(), std::filesystem::current_path().string() );
	throw std::runtime_error( "Failed to read parser file" );
}

std::shared_ptr< ParserInstance > ParserScript::createInstance() const
{
	return this->m_compiled->createInstance();
}