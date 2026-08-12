#pragma once

namespace idhan::hyapi
{

enum HydrusImportResponses
{
	Success = 1,
	AlreadyImported = 2,
	PreviouslyDeleted = 3,
	FailedToImport = 4,
	Vetoed = 7
};

} // namespace idhan::hyapi
