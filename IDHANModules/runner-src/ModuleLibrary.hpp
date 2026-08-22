#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ModuleBase.hpp"
#include "ipc/Protocol.hpp"

namespace idhan::runner
{

//! One dlopened module library and the module instances its factory produced.
class ModuleLibrary
{
	void* m_handle { nullptr };
	void ( *m_deinit )() { nullptr };

	//! Declared after the handle so the instances are destroyed before the library is closed: their
	//! vtables and shared_ptr control blocks live inside the mapped image.
	std::vector< std::shared_ptr< IDHANModule > > m_modules {};

	ModuleLibrary( void* handle, void ( *deinit )(), std::vector< std::shared_ptr< IDHANModule > > modules );

  public:

	ModuleLibrary() = default;
	~ModuleLibrary();

	ModuleLibrary( const ModuleLibrary& ) = delete;
	ModuleLibrary& operator=( const ModuleLibrary& ) = delete;

	ModuleLibrary( ModuleLibrary&& other ) noexcept;
	ModuleLibrary& operator=( ModuleLibrary&& other ) noexcept;

	//! Loads \p path and instantiates its modules.
	/** \param run_init Whether to call the library's init(). Interrogation passes false: describing a
	 *                  library only needs its factory, and skipping init keeps VIPS_INIT off the
	 *                  startup path for every library the server enumerates. */
	[[nodiscard]] static std::expected< ModuleLibrary, std::string > load(
		const std::filesystem::path& path,
		ModuleCallbacks callbacks,
		bool run_init );

	[[nodiscard]] std::size_t size() const { return m_modules.size(); }

	//! \return The module at \p index, or nullptr if the index is out of range.
	[[nodiscard]] std::shared_ptr< IDHANModule > at( std::size_t index ) const;

	//! Describes every module this library exports, in factory order.
	[[nodiscard]] std::vector< ipc::ManifestEntry > manifest() const;

	//! Calls ModuleBase::startup() on every module.
	void startup();

	//! Calls ModuleBase::restart() on every module, to drop caches under memory pressure.
	void reclaim();

	//! Calls ModuleBase::shutdown() on every module.
	void shutdown();
};

//! The MIME types \p module declares, resolved through whichever interface it implements.
[[nodiscard]] std::vector< MimeID > handleableMimesOf( const std::shared_ptr< IDHANModule >& module );

} // namespace idhan::runner
