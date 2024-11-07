//
// Created by kj16609 on 7/23/24.
//

#include "idhan/IDHANClient.hpp"

#include <moc_IDHANClient.cpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QNetworkReply>

#include <spdlog/spdlog.h>

#include <thread>

#include "logging/qt_formatters/qstring.hpp"

namespace idhan
{

	void IDHANClient::attemptQueryVersion()
	{
		QNetworkRequest request;

		QUrl url {};
		url.setHost( QString::fromStdString( m_config.hostname ) );
		url.setPort( m_config.port );
		url.setPath( "/version" );

		if ( m_config.use_ssl )
		{
			url.setScheme( "https" );
		}
		else
		{
			url.setScheme( "http" );
		}

		request.setUrl( url );

		spdlog::info( "Requesting version info from {}", url.toString() );

		QNetworkReply* reply { m_network.get( request ) };

		connect(
			reply,
			&QNetworkReply::finished,
			this,
			[ this, reply ]()
			{
				handleVersionInfo( reply );
				reply->deleteLater();
			} );

		connect(
			reply,
			&QNetworkReply::errorOccurred,
			this,
			[ this, reply ]( QNetworkReply::NetworkError error )
			{
				spdlog::error(
					"Failed to get reply from remote: {}:{}", static_cast< int >( error ), reply->errorString() );

				reply->deleteLater();
			} );
	}

	IDHANClient& IDHANClient::instance()
	{
		return *m_instance;
	}

	IDHANClient::IDHANClient( const IDHANClientConfig& config ) : QObject( nullptr ), m_config( config )
	{
		std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
		spdlog::info( "Hostname: {}", m_config.hostname );
		spdlog::info( "Port: {}", m_config.port );
		if ( m_config.hostname.empty() ) throw std::runtime_error( "hostname must not be empty" );

		if ( m_instance != nullptr ) throw std::runtime_error( "Only one IDHANClient instance should be created" );

		if ( QCoreApplication::instance() == nullptr )
			throw std::runtime_error( "IDHANClient expects a running Qt instance. QGuiApplication of QApplication" );

		attemptQueryVersion();

		m_instance = this;
	}

	void IDHANClient::handleVersionInfo( QNetworkReply* reply )
	{
		spdlog::info( "Recieved version info" );

		const auto data { reply->readAll() };
		const QJsonDocument json { QJsonDocument::fromJson( data ) };

		spdlog::info( "Data recieved: {}", std::string_view( data.data(), data.size() ) );

		const auto version { json[ "idhan_version" ].toInteger() };
		const auto api_version { json[ "idhan_api_version" ].toInteger() };

		spdlog::info(
			"Recieved info from server at {}, IDHAN version: {}, API version: {}",
			reply->url().toString(),
			version,
			api_version );
	}

} // namespace idhan
