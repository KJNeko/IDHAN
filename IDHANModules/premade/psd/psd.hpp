#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ModuleBase.hpp"

namespace psd
{

//! A whole PSD as one contiguous view, however it had to be obtained.
/** Parsing a PSD is random access over the entire file, so it needs the bytes contiguous. When the
 *  input already carries a mapping this borrows it; only when it does not is the file allocated and
 *  copied. Do not outlive the ModuleFile it was opened from. */
class FileContents
{
	//! Filled only on the copying path. Empty whenever m_borrowed has the bytes.
	std::vector< std::uint8_t > m_owned {};
	std::span< const std::uint8_t > m_borrowed {};

  public:

	FileContents() = default;

	explicit FileContents( const std::span< const std::uint8_t > borrowed ) : m_borrowed( borrowed ) {}

	explicit FileContents( std::vector< std::uint8_t > owned ) : m_owned( std::move( owned ) ) {}

	[[nodiscard]] const std::uint8_t* data() const { return m_borrowed.empty() ? m_owned.data() : m_borrowed.data(); }

	[[nodiscard]] std::size_t size() const { return m_borrowed.empty() ? m_owned.size() : m_borrowed.size(); }
};

//! Presents \p file as one contiguous view, copying it only if it does not already have one.
[[nodiscard]] std::expected< FileContents, idhan::ModuleError > openWholeFile( const idhan::ModuleFile& file );

//! Reads a big-endian uint16 from \p data (PSD files are big-endian). Reads 2 bytes.
[[nodiscard]] std::uint16_t readUint16BE( const std::uint8_t* data );

//! Reads a big-endian uint32 from \p data. Reads 4 bytes.
[[nodiscard]] std::uint32_t readUint32BE( const std::uint8_t* data );

//! Reads a big-endian IEEE-754 float from \p data. Reads 4 bytes.
[[nodiscard]] float readFloat32BE( const std::uint8_t* data );

//! The parsed fields of a PSD file header.
struct PSDHeader
{
	std::uint16_t channels; //!< Number of channels (colour + alpha).
	std::uint32_t height; //!< Image height in pixels.
	std::uint32_t width; //!< Image width in pixels.
	std::uint16_t depth; //!< Bits per channel sample (1, 8, 16 or 32).
	std::uint16_t colorMode; //!< PSD colour mode (0 bitmap, 1 grayscale, 2 indexed, 3 RGB, 4 CMYK, ...).
};

//! Parses and validates a PSD header from the start of a file.
//! \param data Pointer to the file bytes.
//! \param length Number of bytes available at \p data.
//! \return The header, or std::nullopt if the signature is wrong, the buffer is too short, or a
//!         field is out of the accepted range (see the dimension/channel clamps in the .cpp).
[[nodiscard]] std::optional< PSDHeader > parsePSDHeader( const std::uint8_t* data, std::size_t length );

//! Decompresses one PackBits (PSD RLE) scanline.
//! \param buffer Compressed input bytes.
//! \param bufferLength Number of compressed bytes available.
//! \param output Destination for the decompressed bytes.
//! \param outputLength Exact number of bytes expected in \p output; decoding stops at this bound.
void unpackScanline(
	const std::uint8_t* buffer,
	std::size_t bufferLength,
	std::uint8_t* output,
	std::size_t outputLength );

//! Decompresses a full RLE-encoded raster into planar (channel-separated) samples.
//! \param buffer Pointer to the image data section.
//! \param offset In/out cursor into \p buffer; advanced past the consumed bytes.
//! \param dataLength Total bytes available in \p buffer.
//! \param width,height Image dimensions in pixels.
//! \param channels Number of channels to decode.
//! \param bytesPerSample Bytes per channel sample (1, 2 or 4), derived from the bit depth.
//! \return The decoded planar data (channels concatenated), or empty on malformed input.
[[nodiscard]] std::vector< std::uint8_t > unpackRaster(
	const std::uint8_t* buffer,
	std::size_t& offset,
	std::size_t dataLength,
	std::uint32_t width,
	std::uint32_t height,
	std::uint16_t channels,
	std::size_t bytesPerSample );

//! Down-converts 16-bit-per-sample data to 8-bit. \param pixelCount Total samples to convert.
[[nodiscard]] std::vector< std::uint8_t > convert16to8bit(
	const std::vector< std::uint8_t >& buffer,
	std::size_t pixelCount );

//! Down-converts 32-bit float samples to 8-bit. \param pixelCount Total samples to convert.
[[nodiscard]] std::vector< std::uint8_t > convert32to8bit(
	const std::vector< std::uint8_t >& buffer,
	std::size_t pixelCount );

//! Normalises samples of the given \p depth (16 or 32) down to 8-bit; passes 8-bit input through.
[[nodiscard]] std::vector< std::uint8_t > convertToTargetDepth(
	const std::vector< std::uint8_t >& buffer,
	std::uint16_t depth,
	std::size_t pixelCount );

//! Converts planar 8-bit CMYK to interleaved RGB.
//! \param cmyk Planar CMYK samples. \param pixelCount Number of pixels.
//! \return RGB bytes, or a ModuleError if \p cmyk is too small for \p pixelCount.
[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertCMYKtoInterleavedRGB(
	std::basic_string_view< std::uint8_t > cmyk,
	std::size_t pixelCount );

//! Converts 8-bit grayscale to interleaved RGB (replicated across channels).
//! \return RGB bytes, or a ModuleError if \p gray is too small for \p pixelCount.
[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertGrayscaleToInterleavedRGB(
	std::basic_string_view< std::uint8_t > gray,
	std::size_t pixelCount );

//! Converts indexed-colour data to interleaved RGB via a palette.
//! \param indexed One palette index per pixel. \param colorTable The RGB palette.
//! \return RGB bytes, or a ModuleError if \p indexed is too small for \p pixelCount.
[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertIndexedToInterleavedRGB(
	std::basic_string_view< std::uint8_t > indexed,
	std::size_t pixelCount,
	std::basic_string_view< std::uint8_t > colorTable );

//! Interleaves planar RGB(A) channels into interleaved RGB, dropping any extra channels.
//! \param channels Number of planar channels present (>= 3).
//! \return RGB bytes, or a ModuleError if \p planarData is too small for \p pixelCount.
[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertPlanarRGBToInterleavedRGB(
	std::basic_string_view< std::uint8_t > planarData,
	std::size_t pixelCount,
	std::uint16_t channels );

//! Converts planar data of an arbitrary PSD colour mode to interleaved RGB by dispatching to the
//! matching per-mode converter above.
//! \param colorMode PSD colour mode (see PSDHeader::colorMode).
//! \param colorTable Palette, used only for indexed mode.
//! \return RGB bytes, or a ModuleError for unsupported modes or undersized input.
[[nodiscard]] std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertPlanarToInterleavedRGB(
	std::basic_string_view< std::uint8_t > planarData,
	std::uint16_t colorMode,
	std::uint32_t width,
	std::uint32_t height,
	std::uint16_t channels,
	std::basic_string_view< std::uint8_t > colorTable );

//! Counts the layers in a PSD file's layer section.
//! \return The layer count, or 0 if the file has no layer section or it cannot be parsed.
[[nodiscard]] std::uint32_t countPSDLayers( const std::uint8_t* data, std::size_t length );

} // namespace psd
