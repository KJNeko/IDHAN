//
// Created by kj16609 on 5/2/25.
//
#include "GraphWidget.hpp"

#include <qgraphicsitem.h>

#include "TagNode.hpp"

GraphWidget::GraphWidget( QWidget* parent ) : QGraphicsView( parent )
{
	QGraphicsScene* scene { new QGraphicsScene( this ) };
	scene->setItemIndexMethod( QGraphicsScene::NoIndex );
	scene->setSceneRect( -200, -200, 400, 400 );
	setScene( scene );
	setCacheMode( CacheBackground );
	setViewportUpdateMode( BoundingRectViewportUpdate );
	setRenderHint( QPainter::Antialiasing );
	setTransformationAnchor( AnchorUnderMouse );
	setDragMode( ScrollHandDrag );
}

void GraphWidget::itemMoved()
{
	using namespace std::chrono_literals;
	if ( !timer.isActive() )
	{
		timer.start( 1000ms / 25, this );
	}
}

void GraphWidget::setRootNode( TagNode* node )
{
	m_tag_nodes.clear();
	scene()->clear();

	scene()->addItem( node );
	m_tag_nodes.insert_or_assign( node->tagID(), node );

	node->spawnRelationships();
}

void GraphWidget::addRelationship( TagNode* left, TagNode* right, TagRelationship::RelationshipType relationship )
{
	this->scene()->addItem( new TagRelationship( left, right, relationship ) );

	itemMoved();
}

void GraphWidget::drawBackground( QPainter* painter, const QRectF& rect )
{
	QGraphicsView::drawBackground( painter, rect );
}

void GraphWidget::timerEvent( [[maybe_unused]] QTimerEvent* event )
{
	const QList< QGraphicsItem* > items { scene()->items() };
	for ( QGraphicsItem* item : items )
	{
		TagNode* node { qgraphicsitem_cast< TagNode* >( item ) };

		if ( node ) node->calculateForces();
	}

	bool items_moved { false };

	for ( const auto item : items )
	{
		TagNode* node { qgraphicsitem_cast< TagNode* >( item ) };
		if ( node ) items_moved |= node->advancePosition();
	}

	if ( !items_moved ) timer.stop();
}