//
// Created by kj16609 on 5/18/22.
//

#ifndef IDHAN_FILEIMPORT_HPP
#define IDHAN_FILEIMPORT_HPP

#include <vector>
#include <string_view>
#include <filesystem> // path, file_size
#include <fstream>  // ifstream
#include <stdexcept> // runtime_error
#include <utility> // move
#include <memory> // unique_ptr

#include "libs/MrMime/mister_mime.hpp"
#include <fgl/_experimental/utility/read_values_from_bytes.hpp>

#include "mediaInfo.hpp"

namespace idhan::fileprocessing {

class Tag
{
	//To implement
	std::string_view text;
};

//Copy this format. (Make a .cpp in src if you wanna make it cleaner (preferable))
struct Media
{
	uint64_t hashID{};
	MrMime::FileType MIMEType{ MrMime::FileType::APPLICATION_UNKNOWN };

	std::filesystem::path path{};

	std::vector<Tag> tags{};

	FileInfo info{};

	[[nodiscard]] explicit Media(std::filesystem::path filepath)
	: path(std::move(filepath))
	{
		using MrMime::header_data_buffer_t;
		const std::size_t size{ std::filesystem::file_size(path) };
		if (size < sizeof(header_data_buffer_t)) throw std::runtime_error(
			path.string() + " is too small to be a media file"
		);

		MrMime::header_data_buffer_t buffer;
		auto ifs{ std::ifstream(path, std::ios::binary) };
		ifs.exceptions(ifs.badbit | ifs.failbit | ifs.eofbit);
		ifs.read(
			reinterpret_cast<char*>(buffer.data()),
			sizeof(buffer)
		);
		MIMEType = MrMime::deduceFileType(buffer);

		// FileInfo stuff

		info.file_size = size;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
		switch (MIMEType)
		{
		using enum MrMime::FileType;
		break;case IMAGE_JPEG:
		{
			assert(buffer[0] == std::byte{0xFF} && buffer[1] == std::byte{0xD8});
			// fgl::read_values_from_bytes(
			// 	???
			// );
			info.header_info = JPGHeader{};
		}
		break;default: throw std::runtime_error(
			path.string() + " unhandled file type"
		);
		}
#pragma GCC diagnostic pop
	}

	// Media(uint64_t hashID)
	// {
	// 	//Probably will never use this
	// }

	// Media(
	// 	uint64_t hashID,
	// 	uint64_t mimeType,
	// 	std::vector<Tag> tags,
	// 	std::filesystem::path path)
	// {
	// 	//I'll implement this later. Feel free to fix order/initalize shit
	// }

	// uint64_t getMime()
	// {
	// }

	// std::vector<Tag> getTags()
	// {
	// 	return tags;
	// }
};









#endif //IDHAN_FILEIMPORT_HPP
