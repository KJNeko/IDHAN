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

class ModuleLibrary
{
	void* m_handle { nullptr };
	void ( *m_deinit )() { nullptr };

	//! Must be released before the library is closed: their vtables and shared_ptr control blocks live
	//! inside the mapped image. The destructor and move assignment clear this before dlclose.
	std::vector< std::shared_ptr< IDHANModule > > m_modules {};

	ModuleLibrary( void* handle, void ( *deinit )(), std::vector< std::shared_ptr< IDHANModule > > modules );

  public:

	ModuleLibrary() = default;
	~ModuleLibrary();

	ModuleLibrary( const ModuleLibrary& ) = delete;
	ModuleLibrary& operator=( const ModuleLibrary& ) = delete;

	ModuleLibrary( ModuleLibrary&& other ) noexcept;
	ModuleLibrary& operator=( ModuleLibrary&& other ) noexcept;

	//! \param run_init Whether to call the library's init(). False when only describing the library,
	//!        which needs just its factory.
	[[nodiscard]] static std::expected< ModuleLibrary, std::string > load(
		const std::filesystem::path& path,
		ModuleCallbacks callbacks,
		bool run_init );

	[[nodiscard]] std::size_t size() const { return m_modules.size(); }

	//! \return The module at \p index, or nullptr if the index is out of range.
	[[nodiscard]] std::shared_ptr< IDHANModule > at( std::size_t index ) const;

	[[nodiscard]] std::vector< ipc::ManifestEntry > manifest() const;

	void startup();

	void reclaim();

	void shutdown();
};

//! The MIME types \p module declares, resolved through whichever interface it implements.
[[nodiscard]] std::vector< MimeID > handleableMimesOf( const std::shared_ptr< IDHANModule >& module );

} // namespace idhan::runner
