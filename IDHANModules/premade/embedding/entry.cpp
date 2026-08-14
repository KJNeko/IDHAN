#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

#include "ModelConfig.hpp"
#include "OnnxEmbedder.hpp"

using namespace idhan;

//! Where this library looks for models.
[[nodiscard]] std::filesystem::path modelsRoot()
{
	if ( const char* const override_path { std::getenv( "IDHAN_EMBEDDING_MODELS" ) }; override_path != nullptr )
		return std::filesystem::path { override_path };

	std::error_code error {};
	const auto self { std::filesystem::read_symlink( "/proc/self/exe", error ) };

	if ( error ) return std::filesystem::path { "models" };

	return self.parent_path() / "models";
}

//! The module instances this library exports.
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
