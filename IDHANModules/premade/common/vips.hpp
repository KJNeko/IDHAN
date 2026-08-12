#pragma once
#include <vips/vips.h>

#include <cstddef>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ModuleFile.hpp"

//! The MIME types the vips modules declare handleable.
/** A set rather than the mime-to-loader map this used to be. Loading now goes through
 *  vips_image_new_from_source, which sniffs the format itself, so naming a specific buffer loader
 *  per MIME would only be a second opinion that could disagree with the first. */
inline static const std::unordered_set< std::string > VIPS_MIMES {
	"image/png", "image/jpeg", "image/webp", "image/gif", "image/heif", "image/svg+xml", "image/tiff"
};

inline std::vector< std::string_view > vipsHandleable()
{
	std::vector< std::string_view > ret {};

	for ( const auto& mime : VIPS_MIMES )
	{
		ret.emplace_back( mime );
	}

	return ret;
}

class VipsModuleSource
{
	const idhan::ModuleFile* m_file { nullptr };
	VipsSourceCustom* m_source { nullptr };

	mutable std::mutex m_mutex {};
	std::size_t m_position { 0 };

	//! Serves a read signal. Short reads are how a source signals end of file, so a read past the
	//! end returning zero is correct rather than an error.
	static gint64 onRead( VipsSourceCustom*, void* const buffer, const gint64 length, void* const user )
	{
		auto* const self { static_cast< VipsModuleSource* >( user ) };
		if ( self == nullptr || buffer == nullptr || length <= 0 ) return 0;

		const std::lock_guard< std::mutex > guard { self->m_mutex };

		const auto count { self->m_file->read(
			std::span< std::byte > { static_cast< std::byte* >( buffer ), static_cast< std::size_t >( length ) },
			self->m_position ) };

		if ( !count ) return -1;

		self->m_position += *count;

		return static_cast< gint64 >( *count );
	}

	//! Serves a seek signal. Connecting one is what makes this a seekable source; without it vips
	//! treats the input as a pipe and buffers the whole thing to disk, undoing the point.
	static gint64 onSeek( VipsSourceCustom*, const gint64 offset, const int whence, void* const user )
	{
		auto* const self { static_cast< VipsModuleSource* >( user ) };
		if ( self == nullptr ) return -1;

		const std::lock_guard< std::mutex > guard { self->m_mutex };

		const auto size { static_cast< gint64 >( self->m_file->size() ) };

		gint64 target { 0 };
		switch ( whence )
		{
			case SEEK_SET:
				target = offset;
				break;
			case SEEK_CUR:
				target = static_cast< gint64 >( self->m_position ) + offset;
				break;
			case SEEK_END:
				target = size + offset;
				break;
			default:
				return -1;
		}

		if ( target < 0 ) return -1;

		// Seeking beyond the end is legal and the following read simply returns nothing, which is how
		// a loader probing for a trailer discovers there is none.
		self->m_position = static_cast< std::size_t >( target );

		return target;
	}

  public:

	explicit VipsModuleSource( const idhan::ModuleFile& file ) : m_file( &file ), m_source( vips_source_custom_new() )
	{
		if ( m_source == nullptr ) return;

		g_signal_connect( m_source, "read", reinterpret_cast< GCallback >( &VipsModuleSource::onRead ), this );
		g_signal_connect( m_source, "seek", reinterpret_cast< GCallback >( &VipsModuleSource::onSeek ), this );
	}

	~VipsModuleSource()
	{
		if ( m_source != nullptr ) g_object_unref( m_source );
	}

	VipsModuleSource( const VipsModuleSource& ) = delete;
	VipsModuleSource& operator=( const VipsModuleSource& ) = delete;
	VipsModuleSource( VipsModuleSource&& ) = delete;
	VipsModuleSource& operator=( VipsModuleSource&& ) = delete;

	[[nodiscard]] bool valid() const { return m_source != nullptr; }

	//! The source as vips's loaders want it. VipsSourceCustom derives from VipsSource in GObject's
	//! sense, so the base is the first member and this is what the VIPS_SOURCE macro does.
	[[nodiscard]] VipsSource* get() const { return reinterpret_cast< VipsSource* >( m_source ); }
};
