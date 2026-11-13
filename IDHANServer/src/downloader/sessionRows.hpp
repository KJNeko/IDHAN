#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::downloader::rows
{
template < typename T >
using DbResult = runtime::Task< std::expected< T, std::string > >;

struct InsertedUrl
{
	DownloadSessionUrlID id {};
	std::int64_t created_at {};
	std::optional< std::int64_t > finished_at {};
};

struct CompletedImport
{
	DownloadSessionUrlID id {};
	std::int64_t finished_at {};
	//! Unset when the record has no stored file.
	std::optional< std::int64_t > size {};
};

struct SavedUrl
{
	DownloadSessionUrlID id {};
	DownloadSessionUrlID parent {};
	std::string url;
	std::string state;
	std::optional< RecordID > record;
};

DbResult< std::vector< SavedUrl > > savedUrls( DownloadSessionID session_id, db::DBContext& db );

struct LoggedError
{
	std::int64_t id {};
	std::int64_t occurred_at {};
};

DbResult< std::optional< std::string > > url(
	DownloadSessionID session_id,
	DownloadSessionUrlID id,
	db::DBContext& db );

//! Takes the session's next id from SessionUrlIds. A parent of 0 creates a root; terminal states are stamped finished.
DbResult< InsertedUrl > insert(
	DownloadSessionID session_id,
	DownloadSessionUrlID parent,
	std::string url,
	std::string state,
	db::DBContext& db );

//! Unset when the URL no longer exists.
DbResult< std::optional< CompletedImport > > completeImport(
	DownloadSessionID session_id,
	DownloadSessionUrlID id,
	RecordID record,
	std::string state,
	db::DBContext& db,
	std::optional< std::int64_t > posted_at = std::nullopt );

//! Returns when the URL finished, unset for non-terminal states. Use fail() for the failed state.
DbResult< std::optional< std::int64_t > > mark(
	DownloadSessionID session_id,
	DownloadSessionUrlID id,
	std::string state,
	db::DBContext& db );

//! Marks the URL failed and records why. Returns when it finished.
DbResult< std::int64_t > fail(
	DownloadSessionID session_id,
	DownloadSessionUrlID id,
	std::string stage,
	std::string message,
	db::DBContext& db );

DbResult< LoggedError > addError(
	DownloadSessionID session_id,
	DownloadSessionUrlID id,
	std::string url,
	std::string lane,
	std::optional< std::int32_t > status,
	std::string message,
	db::DBContext& db );

//! Moves the errors of a URL and its descendants onto the URL, ready for clearRetriedErrors.
DbResult< void > flagErrorsForRetry( DownloadSessionID session_id, DownloadSessionUrlID id, db::DBContext& db );

//! Returns the ids of the removed errors.
DbResult< std::vector< std::int64_t > > clearRetriedErrors(
	DownloadSessionID session_id,
	DownloadSessionUrlID id,
	db::DBContext& db );

//! Deletes the URL's direct children, and through them every descendant. Returns the direct children.
DbResult< std::vector< DownloadSessionUrlID > > removeChildren(
	DownloadSessionID session_id,
	DownloadSessionUrlID id,
	db::DBContext& db );
} // namespace idhan::downloader::rows
