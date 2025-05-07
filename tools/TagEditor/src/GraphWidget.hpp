//
// Created by kj16609 on 5/2/25.
//
#pragma once
#include <QGraphicsView>
#include <QWidget>

#include <qbasictimer.h>

#include "IDHANTypes.hpp"
#include "TagRelationship.hpp"

class TagNode;

class GraphWidget final : public QGraphicsView
{
	Q_OBJECT

  public:

	GraphWidget( QWidget* parent = nullptr );

	void itemMoved();

	std::unordered_map< idhan::TagID, TagNode* > m_tag_nodes {};
	QBasicTimer timer;

  public slots:
	// void zoomIn();
	// void zoomOut();

  public:

	void setRootNode( TagNode* node );
	void addRelationship( TagNode* left, TagNode* right, TagRelationship::RelationshipType relationship );

  protected:

	void drawBackground( QPainter* painter, const QRectF& rect ) override;

  private slots:
	void timerEvent( QTimerEvent* event ) override;
};
