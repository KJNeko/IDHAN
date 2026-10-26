#pragma once

#include "ClusterRoutes.hpp"
#include "DownloaderRoutes.hpp"
#include "HydrusRoutes.hpp"
#include "MaintenanceRoutes.hpp"
#include "RecordRoutes.hpp"
#include "RelationshipRoutes.hpp"
#include "SearchRoutes.hpp"
#include "SystemRoutes.hpp"

namespace idhan::server::api
{

// Adds every fixed route family. Implementations must call this before the
// registry is sealed so duplicate and conflicting declarations fail startup.
void registerRoutes( http::RouteRegistry& );

} // namespace idhan::server::api
