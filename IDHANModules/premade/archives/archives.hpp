//
// Created by kj16609 on 11/25/25.
//
#pragma once

#include <string_view>
#include <vector>

#include "ModuleBase.hpp"

const static std::vector< std::string_view > archive_handleable_mimes {
	"application/zip",
	"application/vnd.comicbook+zip"
};

inline std::vector< std::string_view > getHandleableMimesForArchives()
{
	return archive_handleable_mimes;
}

std::expected< std::string, idhan::ModuleError > encoding( const char* str );

std::expected< std::string, idhan::ModuleError > sanitizeEncoding( const char* str );