#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace idhan::search
{

enum class StepKind
{
	Fetch,
	Fold,
	Page
};

struct SearchStep
{
	std::string label {};
	std::size_t rows { 0 };
	StepKind kind { StepKind::Fetch };
	std::int64_t micros { 0 };
	bool inverted { false };
};

class SearchStats
{
	mutable std::mutex m_mutex {};
	std::vector< SearchStep > m_steps {};

  public:

	SearchStats() = default;
	SearchStats( const SearchStats& ) = delete;
	SearchStats& operator=( const SearchStats& ) = delete;

	void record( std::string label, std::size_t rows, StepKind kind, std::int64_t micros = 0, bool inverted = false );

	[[nodiscard]] std::vector< SearchStep > steps() const;

	[[nodiscard]] std::string summary() const;
};

} // namespace idhan::search
