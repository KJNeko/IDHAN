#pragma once

#include "IDHANTypes.hpp"

namespace idhan::notifications
{

// Notification delivery is intentionally unimplemented. Call only after a missing-state
// transition is recorded or a previously missing file has been successfully restored.
void onFileMissing( RecordID record_id );
void onFileFound( RecordID record_id );

} // namespace idhan::notifications
