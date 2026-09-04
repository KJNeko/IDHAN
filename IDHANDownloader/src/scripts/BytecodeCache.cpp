#include "scripts/BytecodeCache.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#include "logging/format_ns.hpp"

namespace idhan::downloader
{

static std::string describeException( JSContext* context )
{
	const JSValue exception { JS_GetException( context ) };
	const char* text { JS_ToCString( context, exception ) };
	std::string message { text == nullptr ? "unknown compilation failure" : text };

	if ( text != nullptr ) JS_FreeCString( context, text );

	JS_FreeValue( context, exception );
	return message;
}

BytecodeCache::BytecodeCache( std::filesystem::path root ) : m_root( std::move( root ) ), m_runtime( JS_NewRuntime() )
{
	if ( m_runtime == nullptr ) return;

	JS_SetModuleLoaderFunc( m_runtime.get(), normalizeModule, loadModule, this );
	m_context.reset( JS_NewContext( m_runtime.get() ) );
}

BytecodeCache::~BytecodeCache() = default;

char* BytecodeCache::normalizeModule( JSContext* context, const char* base, const char* name, void* opaque )
{
	auto* self { static_cast< BytecodeCache* >( opaque ) };
	const auto resolved {
		self->resolve( name == nullptr ? "" : name, std::filesystem::path { base == nullptr ? "" : base } )
	};

	if ( !resolved )
	{
		JS_ThrowReferenceError( context, "%s", resolved.error().c_str() );
		return nullptr;
	}

	const std::string text { resolved->string() };
	// QuickJS owns this allocation.
	auto* output { static_cast< char* >( js_malloc( context, text.size() + 1 ) ) };

	if ( output == nullptr ) return nullptr;

	std::memcpy( output, text.c_str(), text.size() + 1 );
	return output;
}

JSModuleDef* BytecodeCache::loadModule( JSContext* context, const char* name, void* opaque )
{
	auto* self { static_cast< BytecodeCache* >( opaque ) };
	auto compiled { self->compile( std::filesystem::path { name } ) };

	if ( !compiled )
	{
		JS_ThrowReferenceError( context, "%s", compiled.error().c_str() );
		return nullptr;
	}

	return *compiled;
}

std::expected< JSModuleDef*, std::string > BytecodeCache::compile( const std::filesystem::path& script )
{
	if ( m_context == nullptr ) return std::unexpected( "The bytecode cache has no compilation context" );

	std::ifstream input { script, std::ios::binary };

	if ( !input ) return std::unexpected( format_ns::format( "Unable to read parser script {}", script.string() ) );

	const std::string source { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
	const std::string name { std::filesystem::absolute( script ).lexically_normal().string() };

	// Compilation recursively caches imported modules.
	const JSValue compiled { JS_Eval(
		m_context.get(),
		source.c_str(),
		source.size(),
		name.c_str(),
		JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY ) };

	if ( JS_IsException( compiled ) )
		return std::unexpected( format_ns::format( "{}: {}", script.string(), describeException( m_context.get() ) ) );

	auto* definition { static_cast< JSModuleDef* >( JS_VALUE_GET_PTR( compiled ) ) };

	std::size_t size {};
	const JSBufferPtr written { JS_WriteObject( m_context.get(), &size, compiled, JS_WRITE_OBJ_BYTECODE ),
		                        JSAllocationDeleter { m_context.get() } };
	JS_FreeValue( m_context.get(), compiled );

	if ( written == nullptr ) return std::unexpected( format_ns::format( "Unable to serialise {}", script.string() ) );

	m_entries.insert_or_assign( name, std::vector< std::uint8_t > { written.get(), written.get() + size } );
	return definition;
}

std::expected< std::span< const std::uint8_t >, std::string > BytecodeCache::bytecode(
	const std::filesystem::path& script )
{
	const std::string key { std::filesystem::absolute( script ).lexically_normal().string() };

	const std::scoped_lock lock { m_mutex };

	if ( const auto found { m_entries.find( key ) }; found != m_entries.end() )
		return std::span< const std::uint8_t > { found->second };

	// QuickJS stack checks require compilation to use the runtime's current thread base.
	JS_UpdateStackTop( m_runtime.get() );

	if ( const auto compiled { compile( script ) }; !compiled ) return std::unexpected( compiled.error() );

	const auto entry { m_entries.find( key ) };

	if ( entry == m_entries.end() )
		return std::unexpected( format_ns::format( "{} produced no bytecode", script.string() ) );

	return std::span< const std::uint8_t > { entry->second };
}

std::expected< std::filesystem::path, std::string > BytecodeCache::locate( const std::filesystem::path& target )
{
	static constexpr std::array< std::string_view, 3 > suffixes { ".js", "/index.js", "/src/index.js" };

	if ( std::filesystem::is_regular_file( target ) ) return target;

	for ( const std::string_view suffix : suffixes )
	{
		std::filesystem::path candidate { format_ns::format( "{}{}", target.string(), suffix ) };

		if ( std::filesystem::is_regular_file( candidate ) ) return candidate;
	}

	return std::unexpected( format_ns::format( "Module {} does not exist", target.string() ) );
}

std::expected< std::filesystem::path, std::string > BytecodeCache::resolve(
	const std::string_view specifier,
	const std::filesystem::path& referrer ) const
{
	if ( specifier.empty() ) return std::unexpected( "Empty module specifier" );

	const std::filesystem::path relative { specifier };

	if ( relative.is_absolute() && !referrer.empty() )
		return std::unexpected(
			format_ns::format( "Parser modules cannot be imported by absolute path: {}", specifier ) );

	const bool bare { !relative.is_absolute() && !specifier.starts_with( "./" ) && !specifier.starts_with( "../" ) };
	std::error_code root_error {};
	const auto root { std::filesystem::canonical( m_root, root_error ) };

	if ( root_error )
		return std::unexpected(
			format_ns::format( "Unable to resolve parser directory {}: {}", m_root.string(), root_error.message() ) );

	std::filesystem::path base {};

	if ( bare )
		base = root / packages_directory;
	else if ( referrer.empty() )
		base = root;
	else
		base = referrer.parent_path();

	const auto target { std::filesystem::absolute( base / relative ).lexically_normal() };
	const std::string prefix { format_ns::format( "{}{}", root.string(), std::filesystem::path::preferred_separator ) };

	if ( !target.string().starts_with( prefix ) )
		return std::unexpected( format_ns::format( "Module {} resolves outside the parser directory", specifier ) );

	auto located { locate( target ) };

	if ( !located ) return located;

	std::error_code candidate_error {};
	const auto candidate { std::filesystem::canonical( *located, candidate_error ) };

	if ( candidate_error )
		return std::unexpected(
			format_ns::format( "Unable to resolve module {}: {}", located->string(), candidate_error.message() ) );

	const auto mismatch { std::ranges::mismatch( root, candidate ) };

	if ( mismatch.in1 != root.end() )
		return std::unexpected( format_ns::format( "Module {} resolves outside the parser directory", specifier ) );

	return candidate;
}

} // namespace idhan::downloader
