#pragma once

#include <simdjson.h>
#include <string>
#include <vector>

#include "Parameter.hpp"

struct URLGenerator
{
	explicit URLGenerator( simdjson::dom::element json );

	std::string m_id;
	std::string m_handler;
	std::string m_name;
	std::vector< Parameter > m_parameters;
};
