#include "SearchStats.hpp"

#include <algorithm>
#include <string_view>

#include "logging/format_ns.hpp"

namespace idhan::search
{

// The indent is derived from the kind, so a fold step reads as belonging to the fetch above it
// without the label itself having to carry presentation.
constexpr std::string_view fold_prefix { "    after " };
constexpr std::string_view step_prefix { "  " };

constexpr std::string_view prefixFor( const StepKind kind )
{
	return kind == StepKind::Fold ? fold_prefix : step_prefix;
}

//! Milliseconds, with the precision the magnitude deserves. An index-only tag lookup runs in well
//! under a millisecond, so sub-ms values keep three decimals rather than collapsing to `0.0ms`
//! and hiding the difference between a fast term and a free one.
std::string formatMillis( const std::int64_t micros )
{
	const double ms { static_cast< double >( micros ) / 1000.0 };

	if ( ms < 1.0 ) return format_ns::format( "{:.3f}ms", ms );
	if ( ms < 100.0 ) return format_ns::format( "{:.1f}ms", ms );
	return format_ns::format( "{:.0f}ms", ms );
}


void SearchStats::record(
	std::string label,
	const std::size_t rows,
	const StepKind kind,
	const std::int64_t micros,
	const bool inverted )
{
	const std::lock_guard< std::mutex > lock { m_mutex };
	m_steps.push_back( SearchStep { std::move( label ), rows, kind, micros, inverted } );
}

std::vector< SearchStep > SearchStats::steps() const
{
	const std::lock_guard< std::mutex > lock { m_mutex };
	return m_steps;
}

std::string SearchStats::summary() const
{
	const std::lock_guard< std::mutex > lock { m_mutex };

	if ( m_steps.empty() ) return "search: no steps recorded";

	// Padded to prefix + label, not label alone: the fold prefix is eight characters longer, so
	// padding on the label alone would push every fold's row count out of the column this exists to
	// be read down.
	std::size_t column { 0 };
	for ( const auto& step : m_steps ) column = std::max( column, prefixFor( step.kind ).size() + step.label.size() );

	std::string out { "search steps:" };
	for ( const auto& step : m_steps )
	{
		const auto prefix { prefixFor( step.kind ) };

		out += '\n';
		out += prefix;
		out += step.label;
		out.append( column - prefix.size() - step.label.size(), ' ' );
		out += format_ns::format( "  {:>10} rows", step.rows );
		if ( step.inverted ) out += " (excluded)";
		if ( step.micros > 0 ) out += "  " + formatMillis( step.micros );
	}

	return out;
}

} // namespace idhan::search
