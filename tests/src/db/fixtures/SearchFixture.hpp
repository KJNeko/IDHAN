//
// Created by kj16609 on 7/20/26.
//
#pragma once
#include "MappingFixture.hpp"
#include "core/search/SearchBuilder.hpp"

//! Adds record creation with controllable file_info/video_metadata/image_metadata/
//! image_project_metadata data, for exercising SearchBuilder's sort types end-to-end.
class SearchFixture : public MappingFixture
{
  protected:

	MimeID getMimeId( std::string_view mime_name );

	//! Creates a record with a file_info row that satisfies SearchBuilder's unconditional
	//! `mime_id IS NOT NULL` filter (unlike MappingFixture::createRecord, which leaves mime_id
	//! NULL). cluster_store_time and modified_time are both set to NOW() + time_offset_seconds,
	//! so callers can control relative ordering deterministically without depending on wall-clock
	//! timing between inserts.
	RecordID createSearchableRecord(
		std::string_view data,
		std::int64_t size = 100,
		std::string_view mime_name = "image/jpeg",
		std::int64_t time_offset_seconds = 0 );

	//! Reads back the ordered list of record_ids a raw SQL query produces (column 0).
	std::vector< RecordID > runQuery( const std::string& sql );

	//! Builds a no-filter SearchBuilder query for the given sort and runs it. Always takes the fast
	//! path in construct() (no tags, no system predicates).
	std::vector< RecordID > sortedIds( idhan::SortType type, idhan::SortOrder order = idhan::SortOrder::ASC );
};
