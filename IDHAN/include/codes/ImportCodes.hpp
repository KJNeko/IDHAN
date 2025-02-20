//
// Created by kj16609 on 2/20/25.
//
#pragma once

namespace idhan
{

enum ImportStatus
{
	Success = 1,
	Exists = 2,
	Deleted = 3,
	Failed = 4,
};

enum ImportFailureCode
{
	UnknownReason = 0,
	UnknownMime = 1,
};

} // namespace idhan