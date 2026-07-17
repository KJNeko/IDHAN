//
// Created by kj16609 on 6/25/26.
//
#pragma once
#include <json/json.h>

#include <string>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::api
{

//! Looks up or creates the canonical note row for `text`, returning its NoteID.
//! On UNIQUE conflict the INSERT exception is swallowed and the existing row is fetched.
ExpectedTask< NoteID > findOrCreateNote( DbClientPtr db, std::string text );

//! Fetches all notes attached to `record_id` as a JSON array of {note_id, text} objects.
ExpectedTask< Json::Value > getRecordNotes( DbClientPtr db, RecordID record_id );

} // namespace idhan::api
