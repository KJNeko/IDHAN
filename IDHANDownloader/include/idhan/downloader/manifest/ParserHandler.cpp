#include "ParserHandler.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

ParserHandler::ParserHandler( const element json ) :
  m_id { read< std::string >( json, "id" ) }, m_method { read< std::string >( json, "method" ) },
  m_parameters { parameters( required( json, "parameters" ) ) }
{}
