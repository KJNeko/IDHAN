//
// Created by kj16609 on 7/13/22.
//

#ifndef IDHAN_TAGDATA_HPP
#define IDHAN_TAGDATA_HPP


#include <cstdint>
#include <string>

#include <QString>
#include <QObject>


struct TagData
{
	uint64_t tag_id { 0 };
	uint64_t counter { 0 };
	std::string tagText {};


	std::string concat() const
	{
		return tagText + ( counter != 0 ? " (" + std::to_string( counter ) + ")" : "" );
	}


	QString qt_concat() const
	{
		return QString::fromStdString( concat() );
	}


	TagData() = default;

	TagData( const TagData& data ) = default;

	TagData& operator=( const TagData& data ) = default;


	TagData( const uint64_t tag_id_, const uint64_t counter_, const std::string& tagText_ )
		: tag_id( tag_id_ ), counter( counter_ ), tagText( tagText_ ) {}


	TagData( const uint64_t tag_id_, const uint64_t counter_, const std::string_view tagText_ )
		: tag_id( tag_id_ ), counter( counter_ ), tagText( tagText_.begin(), tagText_.end() ) {}


	~TagData() = default;
};

Q_DECLARE_METATYPE( TagData )

#endif //IDHAN_TAGDATA_HPP
