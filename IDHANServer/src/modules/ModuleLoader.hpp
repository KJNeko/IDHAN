#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "RemoteModule.hpp"
#include "WorkerPool.hpp"

namespace idhan::modules
{

//! One module, as the server knows it: what it can do and how to reach it.
struct ModuleDescriptor
{
	std::size_t library_index { 0 }; //!< Which WorkerPool hosts it.
	//! Position in the vector the library's factory returns. The routing key, and the only thing a
	//! CALL uses to name a module.
	std::size_t module_index { 0 };
	std::string name {}; //!< Logs only. Not unique across modules, not stable across builds.
	ModuleType type { 0 };
	ModuleVersion version {};
	bool thread_safe { false };
	ModuleResidency residency { ModuleResidency::SINGLE_RUN };
	std::vector< std::string > mimes {};
	//! EMBEDDING modules only: the model this one provides, and the width of its vectors.
	std::string model_name {};
	std::uint32_t dimensions { 0 };
};

//! Finds the modules the server can call and routes requests to them by MIME.
class ModuleLoader
{
	std::vector< std::shared_ptr< WorkerPool > > m_pools {};
	std::vector< ModuleDescriptor > m_descriptors {};
	std::vector< std::shared_ptr< RemoteModule > > m_modules {};

	//! MIME to indexes into m_modules, one map per interface.
	std::unordered_map< std::string, std::vector< std::size_t > > m_by_mime_metadata {};
	std::unordered_map< std::string, std::vector< std::size_t > > m_by_mime_thumbnailer {};
	std::unordered_map< std::string, std::vector< std::size_t > > m_by_mime_generator {};

	//! Model name to a single index into m_modules.
	std::unordered_map< std::string, std::size_t > m_by_model_embedding {};

	inline static ModuleLoader* m_instance;

	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > lookup(
		const std::unordered_map< std::string, std::vector< std::size_t > >& index,
		std::string_view mime ) const;

	//! Registers everything one library exports. Returns false if the library was unusable.
	bool registerLibrary( const std::filesystem::path& path );

	//! Services a callback a module raised: a probe, or a re-dispatched thumbnail or generate.
	void serviceCallback( std::shared_ptr< WorkerProcess > worker, ipc::Frame frame );

	//! Applies the process-wide half of IDHAN_HARDEN, before the first worker can exist.
	static void applyHardening();

  public:

	ModuleLoader();
	~ModuleLoader();

	ModuleLoader( const ModuleLoader& ) = delete;
	ModuleLoader& operator=( const ModuleLoader& ) = delete;
	ModuleLoader( ModuleLoader&& ) = delete;
	ModuleLoader& operator=( ModuleLoader&& ) = delete;

	//! \return The process-wide ModuleLoader singleton.
	static ModuleLoader& instance() { return *m_instance; }

	//! Interrogates every library in the modules directory and registers what they export.
	void loadModules();

	//! Stops every worker.
	void unloadModules();

	//! Retires workers that have outgrown their memory ceiling or gone idle.
	void maintainWorkers();

	//! \return The thumbnailer modules that can handle \p mime.
	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > getThumbnailerFor( std::string_view mime ) const;
	//! \return The metadata parser modules that can handle \p mime.
	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > getParserFor( std::string_view mime ) const;
	//! \return The generator modules that can handle \p mime.
	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > getGeneratorsFor( std::string_view mime ) const;

	//! \return The module providing \p model_name, or nullptr if no loaded library offers it.
	[[nodiscard]] std::shared_ptr< RemoteModule > getEmbedderFor( std::string_view model_name ) const;

	//! \return Every registered embedding model, as (name, dimensions) pairs.
	/** Used at startup to register models in the database and create their per-model tables. */
	[[nodiscard]] std::vector< std::pair< std::string, std::uint32_t > > embeddingModels() const;

	[[nodiscard]] const std::vector< ModuleDescriptor >& descriptors() const { return m_descriptors; }
};

} // namespace idhan::modules
