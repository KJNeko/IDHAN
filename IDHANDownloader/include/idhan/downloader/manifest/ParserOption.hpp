#pragma once

#include <optional>
#include <simdjson.h>
#include <string>
#include <variant>
#include <vector>

struct ParserOption
{
	explicit ParserOption( simdjson::dom::element json );

	enum class Type
	{
		BOOLEAN,
		NUMBER,
		STRING,
		STRING_ARRAY
	};
	std::string m_name;
	std::string m_description;
	Type m_type {};
	std::variant< bool, double, std::string, std::vector< std::string > > m_default {};
	std::optional< double > m_minimum {};
	bool m_integer { false };
};
