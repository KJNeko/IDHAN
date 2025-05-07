//
// Created by kj16609 on 5/2/25.
//
#pragma once

#include <QFutureWatcher>

#include "GraphWidget.hpp"
#include "TagRelationship.hpp"
#include "idhan/IDHANClient.hpp"

class TagNode final : public QObject, public QGraphicsItem
{
	Q_OBJECT
	Q_INTERFACES( QGraphicsItem )

	idhan::TagID m_id;
	QString tag_text;
	QList< TagRelationship* > m_relationships {};
	QFutureWatcher< idhan::TagRelationshipInfo > m_relationshipinfo_watcher;
	QFutureWatcher< idhan::TagInfo > m_taginfo_watcher;
	bool m_relationships_spawned { false };

	std::vector< TagNode* > left_siblings {};
	std::vector< TagNode* > right_siblings {};

	std::vector< TagNode* > children {};
	std::vector< TagNode* > parents {};

	std::vector< TagNode* > aliased_tags {};
	std::vector< TagNode* > ideal_tags {};

  public:

	TagNode( GraphWidget* widget, idhan::TagID tag_id );
	void spawnRelationships();

	void addRelationship( TagRelationship* relationship );

	QList< TagRelationship* > edges() const { return m_relationships; }

	enum
	{
		Type = UserType + 1
	};

	int type() const override { return Type; }

	void calculateForces();
	bool advancePosition();

	QRectF boundingRect() const override;
	QPainterPath shape() const override;
	void paint( QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget ) override;

	virtual ~TagNode() {}

	idhan::TagID tagID() const { return m_id; }

  signals:
	void triggerSpawnRelated();
	void triggerGatherName();

  private slots:
	void spawnRelated();
	void getRelatedInfo();
	void gatherName();
	void gotName();

  protected:

	QVariant itemChange( GraphicsItemChange change, const QVariant& value ) override;

  private:

	QPointF new_pos;
	GraphWidget* graph;
};
