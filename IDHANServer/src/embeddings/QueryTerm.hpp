#pragma once

#include <string>

#include "IDHANTypes.hpp"

namespace idhan::embeddings
{
struct QueryTerm
{
	bool m_is_text { false };
	std::string m_text {};
	RecordID m_record_id { 0 };
	float m_weight { 1.0f };
};
} // namespace idhan::embeddings
