#pragma once

#include "db/dbTypes.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::mime
{

//! Puts every mime type the closed set in MimeIDs.hpp declares into the mime table. Runs once
//! migrations have, and does nothing to a row that already matches.
IDHANTask< void > registerMimeTypes( DbClientPtr db );

} // namespace idhan::mime
