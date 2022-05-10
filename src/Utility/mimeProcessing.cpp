//
// Created by kj16609 on 5/10/22.
//

#include "include/MrMime/mister_mime.hpp"
#include "include/Utility/mimeProcessing.hpp"
#include <vector>
#include <cstring>


int getMimeType( std::vector<char> data )
{
	using MrMime::FileType, MrMime::header_data_buffer_t, MrMime::deduceFileType, MrMime::hydrus_compatible_filetype;

	header_data_buffer_t buffer{};
	char* const buffer_ptr{ reinterpret_cast<char*>(buffer.data()) };

	memcpy( buffer_ptr, data.data(), buffer.size());

	return deduceFileType( buffer );
}

