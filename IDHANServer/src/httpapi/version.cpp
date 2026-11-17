#include "version.hpp"

#include "apiVersion.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/versions.hpp"
#include "logging/format_ns.hpp"

namespace idhan::httpapi
{

[[nodiscard]] http::HTTPTask< http::JsonResult< VersionInfo > > version( [[maybe_unused]] http::RequestContext context )
{
	auto version_str { format_ns::format( "{}.{}.{}", IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION ) };
#if defined( FGL_GIT_UNSYNCED ) && FGL_GIT_UNSYNCED == 1
	version_str += "-tainted";
#endif

	const auto api_version_str { format_ns::format( "{}.{}.{}", IDHAN_API_MAJOR, IDHAN_API_MINOR, IDHAN_API_PATCH ) };

	VersionInfo info {};
	info.idhan_server_version.string = version_str;
	info.idhan_server_version.major = IDHAN_MAJOR_VERSION;
	info.idhan_server_version.minor = IDHAN_MINOR_VERSION;
	info.idhan_server_version.patch = IDHAN_PATCH_VERSION;

	info.idhan_api_version.string = api_version_str;
	info.idhan_api_version.major = IDHAN_API_MAJOR;
	info.idhan_api_version.minor = IDHAN_API_MINOR;
	info.idhan_api_version.patch = IDHAN_API_PATCH;

	info.branch = FGL_GIT_BRANCH;
	info.commit = FGL_GIT_COMMIT;
	info.tag = FGL_GIT_TAG;
	info.build = FGL_BUILD_TYPE;
	info.unsynced = FGL_GIT_UNSYNCED == 1 ? true : false;
	info.build_on = IDHAN_BUILD_DATE ", " IDHAN_BUILD_TIME;

	co_return http::JsonResponse< VersionInfo > { std::move( info ) };
}

} // namespace idhan::httpapi
