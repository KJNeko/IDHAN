//
// Created by kj16609 on 9/11/24.
//

#pragma once

#include <IDHAN>
#include <filesystem>
#include <sqlite3.h>

namespace idhan::hydrus
{
class HydrusImporter
{
	sqlite3* master_db { nullptr };
	sqlite3* client_db { nullptr };
	sqlite3* mappings_db { nullptr };
	std::shared_ptr< IDHANClient > m_client;

	void copyTags();
	void copyFiles( std::size_t hy_service_id );

  public:

	HydrusImporter() = delete;
	HydrusImporter( const std::filesystem::path& path, std::shared_ptr< IDHANClient >& client );
	~HydrusImporter();

	void copyHydrusTags() { copyTags(); }
};

} // namespace idhan::hydrus