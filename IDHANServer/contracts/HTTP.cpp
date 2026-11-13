#include <concepts>

#include "idhan/http/server/AccessLogger.hpp"
#include "idhan/http/server/Compression.hpp"
#include "idhan/http/response/FileBodySource.hpp"
#include "idhan/http/response/FileResponse.hpp"
#include "idhan/http/server/HTTPServer.hpp"
#include "idhan/http/streaming/JsonBodySource.hpp"
#include "idhan/http/json/JsonSerializer.hpp"
#include "idhan/http/routing/Middleware.hpp"
#include "idhan/http/streaming/SSEBodySource.hpp"
#include "idhan/http/request/UploadPipeline.hpp"

using namespace idhan::http;
static_assert( !std::is_abstract_v< HTTPServer > );
static_assert( !std::is_abstract_v< FileBodySource > );
static_assert( !std::is_abstract_v< JsonBodySource > );
static_assert( !std::is_abstract_v< SSEBodySource > );
static_assert( std::move_constructible< Response > );
static_assert( std::move_constructible< RouteRegistry > );
static_assert( !std::copy_constructible< RequestContext > );
