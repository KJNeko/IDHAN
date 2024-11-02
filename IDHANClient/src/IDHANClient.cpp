//
// Created by kj16609 on 7/23/24.
//

#include "idhan/IDHANClient.hpp"

#include <moc_IDHANClient.cpp>

#include <QNetworkReply>

namespace idhan
{

	void IDHANClient::attemptQueryVersion()
	{
		QNetworkRequest request;

		QUrl url {};
		url.setHost( QString::fromStdString( m_config.hostname + "/version" ) );
		url.setPort( m_config.port );

		request.setUrl( url );

		QNetworkReply* reply { m_network.get( request ) };
		return;
	}

	IDHANClient::IDHANClient( const IDHANClientConfig& config ) : QObject( nullptr ), m_config( config )
	{
		attemptQueryVersion();
	}

	void IDHANClient::recieveVersionData()
	{}
} // namespace idhan
