#pragma once

#include <cstdint>

namespace idhan
{

//! Outcome of an attempt to import a file.
enum ImportStatus : std::uint8_t
{
	Success = 1, //!< File was newly imported.
	Exists = 2, //!< File was already present; nothing was added.
	Deleted = 3, //!< File matches a record previously marked deleted.
	Failed = 4, //!< Import failed; See `ImportFailureCode`
};

//! Reason an import returned ImportStatus::Failed.
enum ImportFailureCode : std::uint8_t
{
	UnknownReason = 0, //!< Failure with no more specific code.
	UnknownMime = 1, //!< The file's MIME type could not be determined or is unsupported.
};

} // namespace idhan