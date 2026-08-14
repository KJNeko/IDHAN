#include "ModuleLibrary.hpp"

#include <dlfcn.h>
#include <format>
#include <utility>

#include "EmbeddingModule.hpp"
#include "GeneratorModule.hpp"
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

namespace idhan::runner
{

//! Signature of the factory that getModulesFunc returns a pointer to.
using GetModulesFunc = std::vector< std::shared_ptr< IDHANModule > > ( * )( ModuleCallbacks );

[[nodiscard]] std::string dlError()
{
	const char* const message { ::dlerror() };
	return message != nullptr ? std::string { message } : std::string { "unknown dynamic loader error" };
}

ModuleLibrary::ModuleLibrary(
	void* const handle,
	void ( *const deinit )(),
	std::vector< std::shared_ptr< IDHANModule > > modules ) :
  m_handle( handle ),
  m_deinit( deinit ),
  m_modules( std::move( modules ) )
{}

ModuleLibrary::~ModuleLibrary()
{
	// Order matters and is the reverse of construction: drop the instances (whose code lives in the
	// mapped image), then let the library tear its own globals down, then unmap.
	m_modules.clear();

	if ( m_deinit != nullptr ) m_deinit();
	if ( m_handle != nullptr ) ::dlclose( m_handle );
}

ModuleLibrary::ModuleLibrary( ModuleLibrary&& other ) noexcept :
  m_handle( std::exchange( other.m_handle, nullptr ) ),
  m_deinit( std::exchange( other.m_deinit, nullptr ) ),
  m_modules( std::move( other.m_modules ) )
{
	other.m_modules.clear();
}

ModuleLibrary& ModuleLibrary::operator=( ModuleLibrary&& other ) noexcept
{
	if ( this != &other )
	{
		m_modules.clear();
		if ( m_deinit != nullptr ) m_deinit();
		if ( m_handle != nullptr ) ::dlclose( m_handle );

		m_handle = std::exchange( other.m_handle, nullptr );
		m_deinit = std::exchange( other.m_deinit, nullptr );
		m_modules = std::move( other.m_modules );
		other.m_modules.clear();
	}

	return *this;
}

std::expected< ModuleLibrary, std::string > ModuleLibrary::load(
	const std::filesystem::path& path,
	ModuleCallbacks callbacks,
	const bool run_init )
{
	::dlerror();

	// RTLD_NOW rather than the RTLD_LAZY the in-process loader used: a library with an unresolved
	// symbol should fail here, during interrogation, where the server can skip it and say why --
	// not halfway through a thumbnail request. RTLD_LOCAL keeps its symbols out of the global
	// namespace even though nothing else shares this process.
	void* const handle { ::dlopen( path.c_str(), RTLD_NOW | RTLD_LOCAL ) };
	if ( handle == nullptr )
		return std::unexpected( std::format( "dlopen of {} failed: {}", path.string(), dlError() ) );

	const auto fail = [ handle ]( std::string message ) -> std::expected< ModuleLibrary, std::string >
	{
		::dlclose( handle );
		return std::unexpected( std::move( message ) );
	};

	::dlerror();
	auto* const get_modules_func { reinterpret_cast< void* (*)() >( ::dlsym( handle, "getModulesFunc" ) ) };
	if ( get_modules_func == nullptr )
		return fail( std::format( "{} does not export getModulesFunc: {}", path.string(), dlError() ) );

	auto* const init_func { reinterpret_cast< void ( * )() >( ::dlsym( handle, "init" ) ) };
	auto* const deinit_func { reinterpret_cast< void ( * )() >( ::dlsym( handle, "deinit" ) ) };

	if ( run_init && init_func != nullptr ) init_func();

	auto* const factory { reinterpret_cast< GetModulesFunc >( get_modules_func() ) };
	if ( factory == nullptr ) return fail( std::format( "{} returned a null module factory", path.string() ) );

	auto modules { factory( std::move( callbacks ) ) };

	if ( modules.empty() ) return fail( std::format( "{} exports no modules", path.string() ) );

	for ( std::size_t i = 0; i < modules.size(); ++i )
	{
		if ( modules[ i ] == nullptr )
			return fail( std::format( "{} returned a null module at index {}", path.string(), i ) );
	}

	return ModuleLibrary { handle, run_init ? deinit_func : nullptr, std::move( modules ) };
}

std::shared_ptr< IDHANModule > ModuleLibrary::at( const std::size_t index ) const
{
	if ( index >= m_modules.size() ) return nullptr;
	return m_modules[ index ];
}

std::vector< ipc::ManifestEntry > ModuleLibrary::manifest() const
{
	std::vector< ipc::ManifestEntry > entries {};
	entries.reserve( m_modules.size() );

	for ( std::size_t index = 0; index < m_modules.size(); ++index )
	{
		const auto& module { m_modules[ index ] };

		// An embedding module is routed by model name rather than by MIME, so its identity has to
		// reach the host through the manifest -- there is nothing else in the entry that names it.
		std::string model_name {};
		std::uint32_t dimensions { 0 };
		bool supports_text { false };

		if ( ( module->type() & ModuleTypeFlags::EMBEDDING ) != 0 )
		{
			const auto embedder { std::static_pointer_cast< EmbeddingModuleI >( module ) };
			model_name = std::string { embedder->modelName() };
			dimensions = static_cast< std::uint32_t >( embedder->dimensions() );
			// Asked BEFORE startup(): the worker announces its manifest and only then brings modules
			// up (WorkerRunner::run), because --describe must enumerate models without paying session
			// creation for each one. So this has to be answerable from what is on disk -- a text graph
			// and a tokenizer beside it -- and cannot reflect anything learned by loading them.
			//
			// A text tower that turns out to be unloadable, or that fails its parity check, therefore
			// still reports true here and refuses at call time with the reason instead.
			supports_text = embedder->supportsText();
		}

		entries.emplace_back(
			ipc::ManifestEntry {
				.index = index,
				.name = std::string { module->name() },
				.type = module->type(),
				.version = module->version(),
				.thread_safe = module->threadSafe(),
				.residency = module->residency(),
				.rss_ceiling_mb = module->rssCeilingMb(),
				.mimes = handleableMimesOf( module ),
				.model_name = std::move( model_name ),
				.dimensions = dimensions,
				.supports_text = supports_text } );
	}

	return entries;
}

void ModuleLibrary::startup()
{
	for ( const auto& module : m_modules ) module->startup();
}

void ModuleLibrary::reclaim()
{
	for ( const auto& module : m_modules ) module->restart();
}

void ModuleLibrary::shutdown()
{
	for ( const auto& module : m_modules ) module->shutdown();
}

std::vector< std::string > handleableMimesOf( const std::shared_ptr< IDHANModule >& module )
{
	std::vector< std::string_view > declared {};

	const auto type { module->type() };

	// Tested in order and not exclusively, so a module that one day implements two interfaces still
	// reports something rather than nothing. It cannot report both today: handleableMimes() would be
	// ambiguous across two bases, so no module can actually inherit from more than one interface.
	if ( ( type & ModuleTypeFlags::METADATA ) != 0 )
		declared = std::static_pointer_cast< MetadataModuleI >( module )->handleableMimes();
	else if ( ( type & ModuleTypeFlags::THUMBNAILER ) != 0 )
		declared = std::static_pointer_cast< ThumbnailerModuleI >( module )->handleableMimes();
	else if ( ( type & ModuleTypeFlags::GENERATOR ) != 0 )
		declared = std::static_pointer_cast< GeneratorModuleI >( module )->handleableMimes();

	std::vector< std::string > mimes {};
	mimes.reserve( declared.size() );
	for ( const auto mime : declared ) mimes.emplace_back( mime );

	return mimes;
}

} // namespace idhan::runner
