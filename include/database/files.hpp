//
// Created by kj16609 on 6/28/22.
//

#pragma once
#ifndef MAIN_FILES_HPP
#define MAIN_FILES_HPP


#include "database.hpp"
#include <array>
#include <cstdint>

#include <QByteArray>

#include <fgl/types/traits.hpp>

#include "TracyBox.hpp"


template< fgl::traits::byte_type T = std::byte > struct ByteArray32 : public std::array< T, 32 >
{
	using base_t = std::array< T, 32 >;


	[[nodiscard]] explicit ByteArray32( const QByteArray& arry )
	{
		if ( arry.size() != 32 )
		{
			spdlog::error( "ByteArray32: QByteArray size is not 32" );
			throw std::runtime_error( "Hash array size is not 32" );
		}
		memcpy( this->data(), arry.data(), this->size() );
	}


	[[nodiscard]] ByteArray32() = default;

	[[nodiscard]] ByteArray32( const ByteArray32& other ) noexcept = default;


	[[nodiscard]] QByteArray getQByteArray() const
	{
		QByteArray hash_arry;
		hash_arry.resize( static_cast<qsizetype>(this->size()) );
		memcpy( hash_arry.data(), this->data(), this->size() );
		
		return hash_arry;
	}


	[[nodiscard]] std::basic_string_view< std::byte > getView() const noexcept
	{
		return { this->data(), this->size() };
	}
};

using Hash32 = ByteArray32< std::byte >;

[[nodiscard]] uint64_t addFile( const Hash32& sha256, Database db = Database() );

[[nodiscard]] uint64_t getFileID( const Hash32& sha256, const bool add = false, Database db = Database() );

[[nodiscard]] Hash32 getHash( const uint64_t hash_id, Database db = Database() );


// Filepath from hash_id
[[nodiscard]] std::filesystem::path getThubmnailpath( const uint64_t hash_id, Database db = Database() );

[[nodiscard]] std::filesystem::path getFilepath( const uint64_t hash_id, Database db = Database() );


// Filepath from only hash
[[nodiscard]] std::filesystem::path getThumbnailpath( const Hash32& sha256 );

[[nodiscard]] std::filesystem::path getFilepath( const Hash32& sha256 );


#endif // MAIN_FILES_HPP
