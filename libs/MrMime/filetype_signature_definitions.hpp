#pragma once
#ifndef MRMIME_FILETYPE_SIGNATURE_DEFINITIONS_HPP_INCLUDED
#define MRMIME_FILETYPE_SIGNATURE_DEFINITIONS_HPP_INCLUDED

#include <tuple>

#include "filetype_enum.h"
#include "byte_signature.hpp"

namespace MrMime
{
	namespace internal
	{

		using BSS = Byte_Signature_Stream_Starter;

/*
	The order in which these signatures appear is the order in which they're
	checked. Be sure to properly order signatures like "123" bellow "123+".
*/

		constexpr std::tuple signatures{ BSS( IMAGE_JPEG ) << "\xff\xd8",
				BSS( IMAGE_GIF ) << "GIF87a",
				BSS( IMAGE_GIF ) << "GIF89a",
				BSS( IMAGE_PNG ) << "\x89PNG",
				BSS( IMAGE_WEBP ) << SkipBytes( 8 ) << "WEBP",
				BSS( IMAGE_TIFF ) << "II*\x00'",
				BSS( IMAGE_TIFF ) << "MM\x00*",
				BSS( IMAGE_BMP ) << "BM",
				BSS( IMAGE_ICON ) << "\x00\x00\x01\x00",
				BSS( IMAGE_ICON ) << "\x00\x00\x02\x00",
				BSS( APPLICATION_FLASH ) << "CWS",
				BSS( APPLICATION_FLASH ) << "FWS",
				BSS( APPLICATION_FLASH ) << "ZWS",
				BSS( VIDEO_FLV ) << "FLV",
				BSS( APPLICATION_PDF ) << "%PDF",
				BSS( APPLICATION_PSD ) << "8BPS\x00\x01",
				BSS( APPLICATION_PSD ) << "8BPS\x00\x02",
				//BSS( APPLICATION_CLIP ) << "CSFCHUNK",
				BSS( APPLICATION_ZIP ) << "PK\x03\x04",
				BSS( APPLICATION_ZIP ) << "PK\x05\x06",
				BSS( APPLICATION_ZIP ) << "PK\x07\x08",
				BSS( APPLICATION_7Z ) << "7z\xBC\xAF\x27\x1C",
				BSS( APPLICATION_RAR ) << "\x52\x61\x72\x21\x1A\x07\x00",
				BSS( APPLICATION_RAR ) << "\x52\x61\x72\x21\x1A\x07\x01\x00",
				//BSS( APPLICATION_HYDRUS_ENCRYPTED_ZIP ) << "hydrus encrypted zip",
				BSS( VIDEO_MP4 ) << SkipBytes( 4 ) << "ftypmp4",
				BSS( VIDEO_MP4 ) << SkipBytes( 4 ) << "ftypisom",
				BSS( VIDEO_MP4 ) << SkipBytes( 4 ) << "ftypM4V",
				BSS( VIDEO_MP4 ) << SkipBytes( 4 ) << "ftypMSNV",
				BSS( VIDEO_MP4 ) << SkipBytes( 4 ) << "ftypavc1",
				BSS( VIDEO_MP4 ) << SkipBytes( 4 ) << "ftypFACE",
				BSS( VIDEO_MP4 ) << SkipBytes( 4 ) << "ftypdash",
				BSS( VIDEO_MOV ) << SkipBytes( 4 ) << "ftypqt",
				BSS( AUDIO_FLAC ) << "fLaC",
				BSS( AUDIO_WAVE ) << "RIFF" << SkipBytes( 8 ) << "WAVE",
				BSS( VIDEO_AVI ) << SkipBytes( 8 ) << "AVI",
				//BSS( UNDETERMINED_WM ) << "\x30\x26\xB2\x75\x8E\x66\xCF\x11"
				//						  "\xA6\xD9\x00\xAA\x00\x62\xCE\x6C"
		};

		typedef decltype( signatures ) Signatures_t;

		[[maybe_unused]]
		constexpr std::size_t signature_count{ std::tuple_size<Signatures_t>{}};

		[[maybe_unused]] constexpr std::size_t size_of_largest_signature{ std::apply(
				[] < typename ... SIGS >( const SIGS& ... sigs ) constexpr -> std::size_t
				{
					std::size_t largest_size{ 0 };

					auto find_largest{ [&largest_size]( const auto& sig ) constexpr -> void
					{
						const std::size_t size{ sig.size() };
						if ( size > largest_size )
						{
							largest_size = size;
						}
					}};

					(find_largest( sigs ), ...);

					return largest_size;
				}, signatures )
		};

	} // namespace internal
} // namespace MrMime

#endif // MRMIME_FILETYPE_SIGNATURE_DEFINITIONS_HPP_INCLUDED
