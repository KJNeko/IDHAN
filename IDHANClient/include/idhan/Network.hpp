//
// Created by kj16609 on 3/8/25.
//
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPromise>

#include <memory>

#include "fgl/defines.hpp"

namespace idhan
{

enum HttpMethod
{
	POST,
	UPDATE,
	DELETE,
	GET
};

class Network : public QObject
{
	Q_OBJECT

	QNetworkAccessManager m_network;

	QNetworkReply* sendDataI( const HttpMethod method, const QNetworkRequest& request, const QByteArray& body );

  public:

	FGL_DELETE_COPY( Network );
	FGL_DELETE_MOVE( Network );

	Network( QObject* parent = nullptr );

	[[nodiscard]] QNetworkReply*
		send( const HttpMethod method, const QNetworkRequest& request, const QByteArray& body );

  public slots:
	void doSendData(
		const HttpMethod method,
		const QNetworkRequest& request,
		const QByteArray& body,
		const std::shared_ptr< QPromise< QNetworkReply* > >& promise );

  signals:
	void sendData(
		const HttpMethod method,
		const QNetworkRequest& request,
		const QByteArray& body,
		std::shared_ptr< QPromise< QNetworkReply* > > promise );
};
} // namespace idhan