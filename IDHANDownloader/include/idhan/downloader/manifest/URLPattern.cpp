#include "URLPattern.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

URLPattern::URLPattern( element json ) :
  m_name { read< std::string >( json, "name" ) },
  m_generator { optional< std::string >( json, "generator" ) },
  m_patterns { list< Pattern >( json, "patterns", true ) },
  m_remove_query { list< std::string >( json, "removeQuery" ) }
{}
