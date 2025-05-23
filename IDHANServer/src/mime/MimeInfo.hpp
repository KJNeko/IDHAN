//
// Created by kj16609 on 12/18/24.
//
#pragma once
#include <json/value.h>

#include <variant>

#include "FileInfo.hpp"
#include "IDHANTypes.hpp"

namespace idhan::mime
{

struct Resolution
{
	int width, height;
};

struct VideoInfo
{
	Resolution resolution;
};

struct ImageInfo
{
	Resolution resolution;
};

struct AnimatedInfo : public ImageInfo
{};

struct MimeInfo
{
	using SubInfoVariant = std::variant< VideoInfo, ImageInfo, AnimatedInfo >;

	MimeID id { constants::INVALID_MIME_ID };
	SubInfoVariant info {};
	Json::Value extra_info {};

	template < typename T >
		requires std::constructible_from< SubInfoVariant, T >
	MimeInfo( T& variant ) : info( variant )
	{}
};

} // namespace idhan::mime

namespace idhan
{
using namespace idhan::mime;
}
