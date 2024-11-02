//
// Created by kj16609 on 7/24/24.
//

#include "files.hpp"

#include "drogon/HttpAppFramework.h"
#include "logging/log.hpp"

namespace idhan::hyapi
{

	void setupFileHandlers()
	{
		auto& app = drogon::app();

		app.registerHandler( "/hyapi/get_files/search_files", &searchFiles );

		//app.registerHandler( "/hyapi/add_files/add_file", &importFile );
	}

} // namespace idhan::hyapi
