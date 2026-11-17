#include "URLGenerator.hpp"

#include <spdlog/spdlog.h>

#include "JsonDecoding.hpp"

using namespace idhan::downloader::manifest_json;

URLGenerator::URLGenerator( element json ) :
  m_id { read< std::string >( json, "id" ) },
  m_handler { read< std::string >( json, "handler" ) },
  m_name { read< std::string >( json, "name" ) },
  m_parameters { parameters( required( json, "params" ) ) }
{}
