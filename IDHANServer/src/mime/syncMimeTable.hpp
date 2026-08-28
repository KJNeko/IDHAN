#pragma once

#include "db/dbTypes.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::mime
{

//! Makes the mime table match the closed set MimeIDs.hpp declares. The id is the identity and the
//! name is an attribute of it, so renaming a mime in the header renames the row and every file
//! already recorded under that id follows. Runs once migrations have.
IDHANTask< void > syncMimeTable( DbClientPtr db );

} // namespace idhan::mime
