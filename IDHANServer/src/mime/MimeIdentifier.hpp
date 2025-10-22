//
// Created by kj16609 on 10/21/25.
//
#pragma once
#include <json/value.h>

#include <filesystem>
#include <string>
#include <vector>

#include "MimeMatchBase.hpp"

namespace idhan::mime
{
using MimeScore = std::uint_fast16_t;

class MimeIdentifier
{
	std::string m_mime;
	std::vector< std::string > m_extensions;
	std::string m_best_extension { "bin" };

	std::vector< MimeMatcher > m_matchers {};
	MimeScore m_priority { 25 };

  public:

	bool hasMatchers() const { return !m_matchers.empty(); }

	std::string_view mime() const { return m_mime; }

	std::string getBestExtension() const { return m_best_extension; }

	drogon::Task< bool > test( Cursor cursor ) const;

	MimeScore priority() const { return m_priority; }

	MimeIdentifier() = delete;
	MimeIdentifier( const Json::Value& json );
	MimeIdentifier( const std::filesystem::path& path );
};

Json::Value jsonFromFile( const std::filesystem::path& path );

} // namespace idhan::mime