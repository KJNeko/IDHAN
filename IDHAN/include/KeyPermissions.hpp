//
// Created by kj16609 on 11/8/24.
//
#pragma once
#include <limits>

namespace idhan
{
	enum KeyPermissions
	{
		//! Allows for api access in general
		eApiAccess = 1 << 0,
		//! Allows for API access beyond localhost
		eRemoteAccess = 1 << 1,
		//! Allows for requesting thumbnails
		eThumbnailAccess = 1 << 2,
		//! Allows for retrieval of files
		eFileAccess = 1 << 3,
		//! Allows for importing files
		eImportAccess = 1 << 4,
		//! Allows for deleting files
		eDeleteAccess = 1 << 5,
		//! Allows for deleting records
		eRecordDeleteAccess = 1 << 6,
		//! Allow for making of new permissions (cannot give permissions it does not have)
		eCreateAccessKey = 1 << 7,
		//! Allows for creating session keys.
		eIssueSessionKeys = 1 << 8,
		eEditFileTags = 1 << 9,
		eEditFileMetadata = 1 << 10,

		//! Gives all API permissions
		eAllPermissions = ~( 0 )
	};
} // namespace idhan