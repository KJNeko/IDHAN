//
// Created by kj16609 on 8/10/26.
//

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

#include "ModelConfig.hpp"
#include "OnnxEmbedder.hpp"

using namespace idhan;

namespace
{

//! Where this library looks for models.
/** Beside the module .so by default, so a model directory dropped next to the binary is found with
 *  no configuration. IDHAN_EMBEDDING_MODELS overrides it, which is how the worker is pointed at a
 *  shared model store. */
[[nodiscard]] std::filesystem::path modelsRoot()
{
	if ( const char* const override_path { std::getenv( "IDHAN_EMBEDDING_MODELS" ) }; override_path != nullptr )
		return std::filesystem::path { override_path };

	std::error_code error {};
	const auto self { std::filesystem::read_symlink( "/proc/self/exe", error ) };

	if ( error ) return std::filesystem::path { "models" };

	return self.parent_path() / "models";
}

} // namespace

//! The module instances this library exports.
/** Unlike the other premade libraries this vector is not a fixed list: it is one module per model
 *  directory found on disk, so the library's module indices depend on what is installed.
 *
 *  That is safe only because the host compares a manifest signature that now includes each model's
 *  name and dimensionality -- adding or removing a model changes the signature, and a running server
 *  re-interrogates rather than dispatching to a stale index. */
std::vector< std::shared_ptr< IDHANModule > > getModules( ModuleCallbacks callbacks )
{
	const auto root { modelsRoot() };

	auto configs { premade::scanModels( root ) };

	std::vector< std::shared_ptr< IDHANModule > > modules {};
	modules.reserve( configs.size() );

	for ( auto& config : configs )
		modules.emplace_back( std::make_shared< premade::OnnxEmbedder >( callbacks, std::move( config ) ) );

	if ( modules.empty() )
		spdlog::info( "No embedding models found under {}; IDHANEmbedding exports nothing", root.string() );

	// Sorted by model name so the index a model occupies does not depend on directory iteration
	// order, which is not stable across filesystems. Without this, two servers reading the same
	// model store could disagree about which index means which model.
	std::ranges::sort(
		modules,
		[]( const std::shared_ptr< IDHANModule >& lhs, const std::shared_ptr< IDHANModule >& rhs )
		{
			return std::static_pointer_cast< EmbeddingModuleI >( lhs )->modelName()
		         < std::static_pointer_cast< EmbeddingModuleI >( rhs )->modelName();
		} );

	return modules;
}

extern "C" {

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

void init()
{}

void deinit()
{}
}
