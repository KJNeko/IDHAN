//
// Created by kj16609 on 3/6/25.
//

#include "idhan/logging/logger.hpp"

#include <QJsonValue>
#include <QNetworkReply>

namespace idhan::logging
{

void logResponse( QNetworkReply* reply )
{
	if ( !reply ) return;

	const auto data { reply->readAll() };
	if ( data.isEmpty() )
	{
		error( reply->errorString().toStdString() );
		return;
	}

	// there is some data with the reply
	// test if it's json
	if ( reply->header( QNetworkRequest::ContentTypeHeader ).toString().startsWith( "application/json" ) )
	{
		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		// get message if present
		if ( doc[ "error" ].isString() )
		{
			error( "{}: {}", reply->errorString(), doc[ "error" ].toString() );

			return;
		}

		return;
	}

	info( "Failed to process special response: {}", reply->header( QNetworkRequest::ContentTypeHeader ).toString() );
	error( reply->errorString().toStdString() );
}

} // namespace idhan::logging
