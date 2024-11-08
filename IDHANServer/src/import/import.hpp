//
// Created by kj16609 on 8/10/24.
//

#pragma once

#include <QByteArrayView>
#include <QDir>

#include <cstdint>

#include "crypto/sha256.hpp"

namespace idhan::import
{

using FileRecordID = std::uint64_t;

struct FileRecord
{
	FileRecordID m_id;
	SHA256 m_sha256;
};

QFileInfo getPath( const FileRecord record );

FileRecord createRecord( const SHA256& sha256 );

FileRecord createRecord( QByteArrayView data );

FileRecord createRecord( const std::vector< std::byte >& data );

} // namespace idhan::import

namespace idhan
{

using import::FileRecord;

}