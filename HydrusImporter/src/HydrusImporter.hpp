//
// Created by kj16609 on 9/11/24.
//

#pragma once

#include <QFutureSynchronizer>

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
	std::filesystem::path m_path;

	QFutureSynchronizer< void > sync {};

	void copyTags();
	void copyParents();
	void copySiblings();

	void copyFileStorage();

	void copyHashes();

  public:

	HydrusImporter() = delete;
	HydrusImporter( const std::filesystem::path& path, std::shared_ptr< IDHANClient >& client );
	~HydrusImporter();

	void copyHydrusTags();

	void copyFileInfo();

	void copyHydrusInfo();
};

} // namespace idhan::hydrus