#pragma once

#include <simdjson.h>
#include <string>
#include <vector>

#include "Parameter.hpp"

struct ParserHandler
{
	explicit ParserHandler( simdjson::dom::element json );

	std::string m_id;
	std::string m_method;
	std::vector< Parameter > m_parameters {};
};
