#include "MissingFiles.hpp"

namespace idhan::notifications
{

void onFileMissing( [[maybe_unused]] const RecordID record_id )
{
	// TODO: Notify that the stored file was confirmed missing.
}

void onFileFound( [[maybe_unused]] const RecordID record_id )
{
	// TODO: Notify that a previously missing file was recovered.
}

} // namespace idhan::notifications
