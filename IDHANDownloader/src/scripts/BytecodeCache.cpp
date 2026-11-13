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

std::expected< void, std::string > BytecodeCache::merge( const BytecodeCache& prepared )
{
	const std::scoped_lock lock { m_mutex, prepared.m_mutex };

	for ( const auto& [ path, source ] : prepared.m_sources )
	{
		const auto found { m_sources.find( path ) };

		if ( found != m_sources.end() && found->second != source )
		{
			return std::unexpected( "Shared parser dependency changed: " + path + "; update all parsers together" );
		}
	}

	for ( const auto& [ key, path ] : prepared.m_resolutions )
	{
		const auto found { m_resolutions.find( key ) };
		if ( found != m_resolutions.end() && found->second != path )
			return std::unexpected( "Shared parser module resolution changed: " + key.second );
	}
	m_sources.insert( prepared.m_sources.begin(), prepared.m_sources.end() );
	m_entries.insert( prepared.m_entries.begin(), prepared.m_entries.end() );
	m_resolutions.insert( prepared.m_resolutions.begin(), prepared.m_resolutions.end() );
	return {};
}

std::expected< std::span< const std::uint8_t >, std::string > BytecodeCache::preparedBytecode(
	const std::filesystem::path& script ) const
{
	const auto path { ( script.is_absolute() ? script : m_root / script ).lexically_normal() };
	const std::scoped_lock lock { m_mutex };
	const auto found { m_entries.find( path.string() ) };
	if ( found == m_entries.end() )
		return std::unexpected( "Module is not in the prepared parser snapshot: " + path.string() );

	return std::span< const std::uint8_t > { found->second };
}

std::expected< std::filesystem::path, std::string > BytecodeCache::resolvePrepared(
	const std::string_view specifier,
	const std::filesystem::path& referrer ) const
{
	if ( specifier.empty() ) return std::unexpected( "Empty module specifier" );

	const std::filesystem::path relative { specifier };
	if ( relative.is_absolute() && !referrer.empty() )
		return std::unexpected( "Parser modules cannot be imported by absolute path: " + std::string { specifier } );

	const std::scoped_lock lock { m_mutex };
	const auto known { m_resolutions.find( { referrer, std::string { specifier } } ) };
	if ( known != m_resolutions.end() ) return known->second;

	const bool bare { !relative.is_absolute() && !specifier.starts_with( "./" ) && !specifier.starts_with( "../" ) };
	const auto base { bare ? m_root / packages_directory : referrer.empty() ? m_root : referrer.parent_path() };
	const auto target { ( base / relative ).lexically_normal() };
	const auto mismatch { std::ranges::mismatch( m_root, target ) };
	if ( mismatch.in1 != m_root.end() )
		return std::unexpected( "Module resolves outside the parser directory: " + std::string { specifier } );

	if ( m_entries.contains( target.string() ) ) return target;

	static constexpr std::array< std::string_view, 6 > suffixes {
		".js", ".mjs", "/index.js", "/index.mjs", "/src/index.js", "/src/index.mjs"
	};
	for ( const auto suffix : suffixes )
	{
		std::filesystem::path candidate { target.string() + std::string { suffix } };
		if ( m_entries.contains( candidate.string() ) ) return candidate;
	}
	return std::unexpected( "Module is not in the prepared parser snapshot: " + std::string { specifier } );
}

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

	const auto extension { script.extension() };

	if ( extension != ".js" && extension != ".mjs" )
	{
		return std::unexpected( "Parser modules must be JavaScript (.js or .mjs); compile TypeScript externally" );
	}

	const std::string name { std::filesystem::absolute( script ).lexically_normal().string() };

	std::string source;
	const auto emitted { m_sources.find( name ) };

	if ( emitted != m_sources.end() )
	{
		source = emitted->second;
	}
	else
	{
		std::ifstream input { script, std::ios::binary };

		if ( !input ) return std::unexpected( format_ns::format( "Unable to read parser script {}", script.string() ) );

		source.assign( std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} );
		m_sources.emplace( name, source );
	}

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

	m_entries.try_emplace( name, written.get(), written.get() + size );
	return definition;
}

std::expected< std::span< const std::uint8_t >, std::string > BytecodeCache::bytecode(
	const std::filesystem::path& script )
{
	const std::scoped_lock lock { m_mutex };
	const auto frozen_key { std::filesystem::absolute( script ).lexically_normal().string() };
	const auto frozen { m_entries.find( frozen_key ) };

	if ( frozen != m_entries.end() ) return std::span< const std::uint8_t > { frozen->second };

	std::error_code error;
	const auto canonical {
		m_sources.contains( frozen_key ) ?
			std::filesystem::path { frozen_key } :
			std::filesystem::canonical( script, error )
	};

	if ( error )
	{
		return std::unexpected(
			format_ns::format( "Unable to locate parser {}: {}", script.string(), error.message() ) );
	}

	const std::string key { canonical.string() };

	if ( const auto found { m_entries.find( key ) }; found != m_entries.end() )
		return std::span< const std::uint8_t > { found->second };

	// QuickJS stack checks require compilation to use the runtime's current thread base.
	JS_UpdateStackTop( m_runtime.get() );

	const auto compiled_e { compile( canonical ) };

	if ( !compiled_e.has_value() ) return std::unexpected( compiled_e.error() );

	const auto entry { m_entries.find( key ) };

	if ( entry == m_entries.end() )
		return std::unexpected( format_ns::format( "{} produced no bytecode", script.string() ) );

	return std::span< const std::uint8_t > { entry->second };
}

std::expected< std::filesystem::path, std::string > BytecodeCache::locate( const std::filesystem::path& target )
{
	static constexpr std::array< std::string_view, 6 > suffixes {
		".js", ".mjs", "/index.js", "/index.mjs", "/src/index.js", "/src/index.mjs"
	};

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

	// Prepared modules keep resolving to the checked source even after files change or disappear.
	{
		const std::scoped_lock lock { m_mutex };
		if ( m_sources.contains( target.string() ) )
		{
			m_resolutions.insert_or_assign( { referrer, std::string { specifier } }, target );
			return target;
		}
	}

	const auto located_e { locate( target ) };

	if ( !located_e.has_value() ) return std::unexpected( located_e.error() );

	const auto& located { located_e.value() };
	std::error_code candidate_error {};
	const auto candidate { std::filesystem::canonical( located, candidate_error ) };

	if ( candidate_error )
		return std::unexpected(
			format_ns::format( "Unable to resolve module {}: {}", located.string(), candidate_error.message() ) );

	const auto mismatch { std::ranges::mismatch( root, candidate ) };

	if ( mismatch.in1 != root.end() )
		return std::unexpected( format_ns::format( "Module {} resolves outside the parser directory", specifier ) );

	{
		const std::scoped_lock lock { m_mutex };
		m_resolutions.insert_or_assign( { referrer, std::string { specifier } }, candidate );
	}
	return candidate;
}

} // namespace idhan::downloader
