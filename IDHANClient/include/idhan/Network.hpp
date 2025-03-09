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

class Network : public QObject
{
	Q_OBJECT

	QNetworkAccessManager m_network;

  public:

	FGL_DELETE_COPY( Network );
	FGL_DELETE_MOVE( Network );

	Network( QObject* parent = nullptr );

	[[nodiscard]] QNetworkReply* post( const QNetworkRequest& request, const QByteArray& body );
	[[nodiscard]] QNetworkReply* get( const QNetworkRequest& request );

  public slots:
	void doSendPost(
		const QNetworkRequest& request, const QByteArray& body, std::shared_ptr< QPromise< QNetworkReply* > > promise );
	void doSendGet( const QNetworkRequest& request, std::shared_ptr< QPromise< QNetworkReply* > > promise );

  signals:
	void sendPost(
		const QNetworkRequest& request, const QByteArray& body, std::shared_ptr< QPromise< QNetworkReply* > > promise );
	void sendGet( const QNetworkRequest& request, std::shared_ptr< QPromise< QNetworkReply* > > promise );
};
} // namespace idhan