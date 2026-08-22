#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "RemoteModule.hpp"
#include "WorkerPool.hpp"

namespace idhan::modules
{

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
	std::vector< MimeID > mimes {};
	std::string model_name {};
	std::uint32_t dimensions { 0 };
};

class ModuleLoader
{
	std::vector< std::shared_ptr< WorkerPool > > m_pools {};
	std::vector< ModuleDescriptor > m_descriptors {};
	std::vector< std::shared_ptr< RemoteModule > > m_modules {};

	std::unordered_map< MimeID, std::vector< std::size_t > > m_by_mime_metadata {};
	std::unordered_map< MimeID, std::vector< std::size_t > > m_by_mime_thumbnailer {};
	std::unordered_map< MimeID, std::vector< std::size_t > > m_by_mime_generator {};
	std::unordered_map< MimeID, std::vector< std::size_t > > m_by_mime_parser {};

	std::unordered_map< std::string, std::size_t > m_by_model_embedding {};

	inline static ModuleLoader* m_instance;

	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > lookup(
		const std::unordered_map< MimeID, std::vector< std::size_t > >& index,
		MimeID mime_id ) const;

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

	static ModuleLoader& instance() { return *m_instance; }

	void loadModules();

	void unloadModules();

	//! Retires workers that have outgrown their memory ceiling or gone idle.
	void maintainWorkers();

	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > getThumbnailerFor( MimeID mime_id ) const;
	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > getParserFor( MimeID mime_id ) const;
	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > getGeneratorsFor( MimeID mime_id ) const;
	[[nodiscard]] std::vector< std::shared_ptr< RemoteModule > > getMimeParserFor( MimeID mime_id ) const;

	[[nodiscard]] std::shared_ptr< RemoteModule > getEmbedderFor( std::string_view model_name ) const;

	[[nodiscard]] std::vector< std::pair< std::string, std::uint32_t > > embeddingModels() const;

	[[nodiscard]] const std::vector< ModuleDescriptor >& descriptors() const { return m_descriptors; }
};

} // namespace idhan::modules
