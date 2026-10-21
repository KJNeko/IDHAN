#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "IDHANTypes.hpp"
#include "MetadataInfo.hpp"

namespace idhan
{
class ModuleFile;

//! JPEG quantization table in zigzag order.
using QuantizationTable = std::array< std::uint16_t, 64 >;

//! Maximum retained comment length in bytes.
constexpr std::size_t COMMENT_LIMIT { 255 };

struct FileEncoding
{
	//! Absent when encoding information is unavailable.
	std::optional< EncodingQuality > quality {};
	//! Sanitized JPEG COM text. Empty if no text was retained; absent if comments were not parsed.
	std::optional< std::string > comment {};
};

//! Read encoding metadata without decoding pixels. JPEG parsing stops at the entropy-coded
//! scan.
[[nodiscard]] FileEncoding readFileEncoding( const ModuleFile& file, MimeID mime_id );

//! Estimate IJG quality (1-100) by minimizing absolute error against scaled standard tables.
//! Ties select the lower quality.
[[nodiscard]] std::uint8_t estimateJpegQuality(
	const QuantizationTable& luma,
	std::optional< QuantizationTable > chroma );

} // namespace idhan
