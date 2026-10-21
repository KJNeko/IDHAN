#include <concepts>

#include "idhan/http/AccessLogger.hpp"
#include "idhan/http/Compression.hpp"
#include "idhan/http/FileBodySource.hpp"
#include "idhan/http/FileResponse.hpp"
#include "idhan/http/HTTPServer.hpp"
#include "idhan/http/JsonBodySource.hpp"
#include "idhan/http/JsonSerializer.hpp"
#include "idhan/http/Middleware.hpp"
#include "idhan/http/SSEBodySource.hpp"
#include "idhan/http/UploadPipeline.hpp"

using namespace idhan::http;
static_assert( !std::is_abstract_v< HTTPServer > );
static_assert( !std::is_abstract_v< FileBodySource > );
static_assert( !std::is_abstract_v< JsonBodySource > );
static_assert( !std::is_abstract_v< SSEBodySource > );
static_assert( std::move_constructible< Response > );
static_assert( std::move_constructible< RouteRegistry > );
static_assert( !std::copy_constructible< RequestContext > );
