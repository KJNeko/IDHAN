//
// Created by kj16609 on 5/3/25.
//
#pragma once
#include <QGraphicsItem>

class TagNode;

class TagRelationship : public QObject, public QGraphicsItem
{
	Q_OBJECT
	Q_INTERFACES( QGraphicsItem )

  public:

	enum class RelationshipType
	{
		ParentChild,
		Sibling,
		Aliased
	} m_relationship;

	using enum RelationshipType;

  private:

	// Parent/OlderSibling/AliasedTag
	union
	{
		TagNode* m_left;
		TagNode* child;
		TagNode* older_sibling;
		TagNode* aliased_tag;
	};

	// Child/YoungerSibling/IdealTag
	union
	{
		TagNode* m_right;
		TagNode* parent;
		TagNode* younger_sibling;
		TagNode* ideal_tag;
	};

	QPointF source_point;

	QPointF target_point;
	qreal arrow_size { 10 };

  public:

	TagRelationship( TagNode* left, TagNode* right, RelationshipType relationship );

	TagNode* leftNode() const { return m_left; }

	TagNode* rightNode() const { return m_right; }

	void adjust();
	QRectF boundingRect() const override;
	void paint( QPainter* painter, const QStyleOptionGraphicsItem*, QWidget* ) override;

	enum
	{
		Type = UserType + 2
	};

	int type() const override { return Type; }
};
