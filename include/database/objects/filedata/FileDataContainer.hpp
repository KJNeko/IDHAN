//
// Created by kj16609 on 8/15/22.
//


#pragma once
#ifndef IDHAN_FILEDATA_HPP
#define IDHAN_FILEDATA_HPP

#include <string>
#include <vector>

struct IDHANRenderHints
{
  private:
	uint8_t flags;

  public:

	//! Indicates that the file is an image
	bool has_image();

	//! An audio stream is present in the file
	bool has_audio();

	//! More then one frame is present in the file
	/*! Note that has_image() and has_frames() can both be true. This indicates a more trivial rendering method. (Such as GIF/APNG) */
	bool has_frames();

	//! Indicates that rendering is required
	/*! If render_required() is true then more advanced rendering is required. This can be for example a PDF or some other normally unsupported formats such as (fuck) adobe photoshop files*/
	bool render_required();
};

//! Metadata info for a file
struct IDHANMetaInfo
{
	//! Resolution x
	uint64_t res_x;

	//! Resolution y
	uint64_t res_y;

	//! Frame count
	/*! frame_count will be '1' in the event of an image or if only one frame is present. It is usually safe to render it as a static image if this is 1. But check has_frames() first since it does more extensive checks or could be user-set along with render_required() for special filetpyes */
	uint64_t frame_count;

	//! Frame information. Accessing anything but frame_count if frame_count is less than or equal to 1 is undefined behaviour
	struct FrameInfo
	{
		//! Frame count
		uint64_t frame_count;

		//! Number of frames to render per second.
		float frames_per_second;

	} frame_info;

	//! Original name of the file
	/*! This is the original name of the file during the import. If empty then the filename was either not populated during import or deleted. */
	std::string original_name;

	//! Byte size of the file
	uint64_t byte_size;

	//! Additional hashes.
	/*! Hashes generated during the import process for a file. */
	struct Hashes
	{
		//! sha256 hash
		std::array<std::byte, 32> sha256;

		//! md5 hash. Legacy (gelbooru)
		std::array<std::byte, 16> md5;
	} hashes;



};

//! Data Container for FileData
/*!
 * This is the container for FileData's shared pointer. It should almost never be seperated or copied from it's FileData
 * */
class FileDataContainer
{
  public:


	//! ID of the data this container is for
	uint64_t file_id;

	//! Path to the file on disk
	std::filesystem::path file_path;

	//! Path to the thumbnail of the file.
	std::filesystem::path thumbnail_path;

	//! List of tags attached to the file
	std::vector<Tag> tags;

	//! Hints on how to render the file
	IDHANRenderHints render_hints;

	//! Pre-Defined flags
	IDHANMetaInfo meta_flags;


	FileDataContainer() = delete;

	FileDataContainer(FileDataContainer& other) = delete;

	FileDataContainer(FileDataContainer&& other) = default;


};


#endif	// IDHAN_FILEDATA_HPP
