//
// Created by kj16609 on 3/6/25.
//

#include "idhan/logging/logger.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>

namespace idhan::logging
{

std::shared_ptr< spdlog::logger > getLogger()
{
	const auto& ctx { IDHANClient::instance() };
	return ctx.getLogger();
}

void logResponse( QNetworkReply* reply )
{
	if ( !reply ) return;

	// there is some data with the reply
	// test if it's json

	const auto header = reply->header( QNetworkRequest::ContentTypeHeader );
	if ( header.isValid() && header.toString().contains( "application/json" ) )
	{
		const auto body { reply->readAll() };
		QJsonDocument doc { QJsonDocument::fromJson( body ) };
		if ( doc.isObject() )
		{
			QJsonObject object { doc.object() };
			if ( object.contains( "error" ) )
			{
				const auto error_msg { object[ "error" ].toString().toStdString() };
				error( error_msg );
				return;
			}
		}
	}

	error( reply->errorString().toStdString() );
}

} // namespace idhan::logging
