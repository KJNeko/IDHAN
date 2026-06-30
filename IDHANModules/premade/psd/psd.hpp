//
// Created by kj16609 on 11/25/25.
//
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

#include "ModuleBase.hpp"

namespace psd
{

[[nodiscard]] std::uint16_t readUint16BE( const std::uint8_t* data );

[[nodiscard]] std::uint32_t readUint32BE( const std::uint8_t* data );

[[nodiscard]] float readFloat32BE( const std::uint8_t* data );

struct PSDHeader
{
	std::uint16_t channels;
	std::uint32_t height;
	std::uint32_t width;
	std::uint16_t depth;
	std::uint16_t colorMode;
};

[[nodiscard]] std::optional< PSDHeader > parsePSDHeader( const std::uint8_t* data, std::size_t length );

void unpackScanline(
	const std::uint8_t* buffer,
	std::size_t bufferLength,
	std::uint8_t* output,
	std::size_t outputLength );

[[nodiscard]] std::vector< std::uint8_t > unpackRaster(
	const std::uint8_t* buffer,
	std::size_t& offset,
	std::size_t dataLength,
	std::uint32_t width,
	std::uint32_t height,
	std::uint16_t channels );

[[nodiscard]] std::vector< std::uint8_t > convert16to8bit( const std::vector< std::uint8_t >& buffer, std::size_t pixelCount );

[[nodiscard]] std::vector< std::uint8_t > convert32to8bit( const std::vector< std::uint8_t >& buffer, std::size_t pixelCount );

[[nodiscard]] std::vector< std::uint8_t > convertToTargetDepth(
	const std::vector< std::uint8_t >& buffer,
	std::uint16_t depth,
	std::size_t pixelCount );

[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertCMYKtoInterleavedRGB(
	std::basic_string_view< std::uint8_t > cmyk,
	std::size_t pixelCount );

[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertGrayscaleToInterleavedRGB(
	std::basic_string_view< std::uint8_t > gray,
	std::size_t pixelCount );

[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertIndexedToInterleavedRGB(
	std::basic_string_view< std::uint8_t > indexed,
	std::size_t pixelCount,
	std::basic_string_view< std::uint8_t > colorTable );

[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertPlanarRGBToInterleavedRGB(
	std::basic_string_view< std::uint8_t > planarData,
	std::size_t pixelCount,
	std::uint16_t channels );

[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertPlanarToInterleavedRGB(
	std::basic_string_view< std::uint8_t > planarData,
	std::uint16_t colorMode,
	std::uint32_t width,
	std::uint32_t height,
	std::uint16_t channels,
	std::basic_string_view< std::uint8_t > colorTable );

[[nodiscard]] std::uint32_t countPSDLayers( const std::uint8_t* data, std::size_t length );

} // namespace psd
