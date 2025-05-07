//
// Created by kj16609 on 5/2/25.
//
#include "TagNode.hpp"

#include <moc_TagNode.cpp>

#include <QVector2D>

#include "TagRelationship.hpp"

TagNode::TagNode( GraphWidget* widget, const idhan::TagID tag_id ) :
  m_id( tag_id ),
  tag_text( "loading..." ),
  graph( widget )
{
	FGL_ASSERT( graph, "Graph was nullptr!" );

	setFlag( ItemIsMovable );
	setFlag( ItemSendsGeometryChanges );
	setCacheMode( DeviceCoordinateCache );
	setZValue( -1 );

	connect( this, &TagNode::triggerSpawnRelated, this, &TagNode::getRelatedInfo );
	connect( this, &TagNode::triggerGatherName, this, &TagNode::gatherName );
	connect(
		&m_relationshipinfo_watcher, &decltype( m_relationshipinfo_watcher )::finished, this, &TagNode::spawnRelated );

	// spawnRelationships();
	emit triggerGatherName();
}

void TagNode::spawnRelationships()
{
	if ( m_relationships_spawned ) return;
	m_relationships_spawned = true;
	emit triggerSpawnRelated();
}

void TagNode::addRelationship( TagRelationship* relationship )
{
	m_relationships.push_back( relationship );
}

double modifier( const double x, const double a )
{
	constexpr double clamp { 50.0 };
	constexpr double div { 30.0 };
	return std::clamp< double >( std::pow( x - a, 5.0 ) / div, -clamp, clamp );
}

void TagNode::calculateForces()
{
	QVector2D force_direction { 0, 0 };
	qreal largest_dist { 0.0f };

	FGL_ASSERT( graph, "Graph was nullptr!" );

	const QList< QGraphicsItem* > items = graph->scene()->items();

	const auto cur_pos { this->pos() };
	if ( std::isnan( cur_pos.x() ) || std::isnan( cur_pos.y() ) )
	{
		new_pos = { 0.0f, 0.0f };
		return;
	}

	/*
	for ( const QGraphicsItem* item : items )
	{
		const TagRelationship* relationship { qgraphicsitem_cast< const TagRelationship* >( item ) };
		if ( !relationship ) continue;

		// the left node is, children, older_siblings, and aliased_tags
		if ( relationship->leftNode() != this ) continue;

		// Children are pulled toward parents,
		// Older siblings are pulled toward younger siblings (TOOD: Reverse this)
		// Aliased tags are pulled closer to their alias

		const TagNode* other { relationship->rightNode() };
	}
	*/

	// Push nodes away from each other
	for ( const QGraphicsItem* item : items )
	{
		const TagNode* other = qgraphicsitem_cast< const TagNode* >( item );
		if ( !other || other == this ) continue;

		const QVector2D vec_to_target { other->pos() - pos() };
		QVector2D vec_to_target_normalized { vec_to_target.normalized() };
		qreal distance = vec_to_target.length();

		bool has_relationship { false };
		for ( const auto& relationship : std::as_const( m_relationships ) )
		{
			if ( relationship->rightNode() == other )
			{
				has_relationship = true;
				break;
			}
		}

		// We have a relationship with this node, So we should only apply relationship weights.
		if ( has_relationship ) continue;

		if ( std::isnan( distance ) || std::isinf( distance ) ) continue;

		if ( distance < 1.0f )
		{
			// If the nodes are right on top of eachother, move them.
			new_pos = pos() + QPointF( rand() % 50, rand() % 50 );
			return;
		}

		constexpr double range { 80 }; // leeway around the target
		constexpr double target { 300 }; // target
		constexpr double offset { target / range };

		//remap the distance from [-200,200] (-offset,offset)  to [-1,1]
		const double normalized_distance = ( distance / range );

		const auto mod { modifier( normalized_distance, offset ) };

		if ( mod > 0.0f ) continue;

		vec_to_target_normalized *= ( mod );

		force_direction += vec_to_target_normalized;
	}
	/*
	// Add constant force pulling toward center
	constexpr double center_pull { 0.5 };
	QVector2D to_center { -pos() };
	if ( to_center.length() > 1.0 )
	{
		to_center.normalize();
		force_direction += to_center * center_pull;
	}
	*/

	for ( const QGraphicsItem* item : items )
	{
		const TagRelationship* relationship { qgraphicsitem_cast< const TagRelationship* >( item ) };
		if ( !relationship ) continue;

		// the left node is, children, older_siblings, and aliased_tags
		if ( relationship->leftNode() != this ) continue;

		// Children are pulled toward parents,
		// Older siblings are pulled toward younger siblings (TOOD: Reverse this)
		// Aliased tags are pulled closer to their alias

		const TagNode* other { relationship->rightNode() };

		QVector2D target_vector { other->pos() - pos() };
		const auto distance { target_vector.length() };

		target_vector.normalize();

		constexpr double range { 80 }; // leeway around the target
		constexpr double target { 300 }; // target
		constexpr double offset { target / range };
		const double normalized_distance { distance / range };

		const auto mod { modifier( normalized_distance, offset ) };

		// Pull toward parent, older sibling, alias
		force_direction += target_vector * mod;
	}

	auto xvel { force_direction.x() };
	auto yvel { force_direction.y() };

	// Limit maximum velocity
	// if ( qAbs( xvel ) < 0.01 ) xvel = 0;
	// if ( qAbs( yvel ) < 0.01 ) yvel = 0;

	new_pos = pos() + QPointF( xvel, yvel );
}

bool TagNode::advancePosition()
{
	if ( new_pos == pos() ) return false;

	setPos( new_pos );
	return true;
}

const QFont FONT { "Arial", 12 };

QRectF TagNode::boundingRect() const
{
	// The bounding rect for a tag node should be the text of the tag
	const QFontMetrics metrics( FONT );
	QRectF bounds { metrics.boundingRect( tag_text ) };

	// Add some padding around the text
	bounds.adjust( -10, -10, 10, 10 );
	return bounds;
}

QPainterPath TagNode::shape() const
{
	return QGraphicsItem::shape();
}

void TagNode::paint( QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
	QRectF bounds = boundingRect();

	// Draw rectangle
	painter->setPen( QPen( Qt::black, 2 ) );
	painter->setBrush( QBrush( Qt::white ) );
	painter->drawRect( bounds );

	// Draw text
	painter->setPen( Qt::black );
	painter->setFont( FONT );
	painter->drawText( bounds, Qt::AlignCenter, tag_text );
}

TagNode* checkNodes( std::unordered_map< idhan::TagID, TagNode* >& nodes, const idhan::TagID& id, GraphWidget* graph )
{
	if ( auto itter = nodes.find( id ); itter != nodes.end() ) return itter->second;

	// Node doesn't exist.
	idhan::logging::info( "Spawning node {}", id );
	TagNode* other { new TagNode( graph, id ) };
	nodes.insert_or_assign( id, other );
	graph->scene()->addItem( other );

	return other;
}

void TagNode::spawnRelated()
{
	const auto& future { m_relationshipinfo_watcher.future() };

	if ( future.isFinished() )
	{
		const idhan::TagRelationshipInfo& info { future.result() };

		const auto& [ aliased, aliases, parents, children, older_siblings, younger_siblings ] = info;

		auto& existing_nodes { graph->m_tag_nodes };

		for ( const auto& aliased_id : aliased )
		{
			auto* node { checkNodes( existing_nodes, aliased_id, graph ) };
			graph->addRelationship( node, this, TagRelationship::Aliased );
			node->spawnRelationships();
		}

		for ( const auto& alias : aliases )
		{
			auto* node { checkNodes( existing_nodes, alias, graph ) };
			graph->addRelationship( this, node, TagRelationship::Aliased );
			node->spawnRelationships();
		}

		/*
		for ( const auto& parent_id : parents )
		{
			auto* node { checkNodes( existing_nodes, parent_id, graph ) };
			graph->addRelationship( node, this, TagRelationship::ParentChild );
			// node->spawnRelationships();
		}

		for ( const auto& child_id : children )
		{
			auto* node { checkNodes( existing_nodes, child_id, graph ) };
			graph->addRelationship( this, node, TagRelationship::ParentChild );
			// node->spawnRelationships();
		}

		for ( const auto& older_sibling_id : older_siblings )
		{
			auto* node { checkNodes( existing_nodes, older_sibling_id, graph ) };
			graph->addRelationship( node, this, TagRelationship::Sibling );
			// node->spawnRelationships();
		}

		for ( const auto& younger_sibling_id : younger_siblings )
		{
			auto* node { checkNodes( existing_nodes, younger_sibling_id, graph ) };
			graph->addRelationship( this, node, TagRelationship::Sibling );
			// node->spawnRelationships();
		}
		*/

		// add node to graph
	}
	else
	{
		throw std::runtime_error( "Huh?" );
	}
}

void TagNode::getRelatedInfo()
{
	const auto m_domain { 5 };
	auto future { idhan::IDHANClient::instance().getTagRelationships( m_id, m_domain ) };

	m_relationshipinfo_watcher.setFuture( future );
}

void TagNode::gatherName()
{
	m_taginfo_watcher.setFuture( idhan::IDHANClient::instance().getTagInfo( m_id ) );

	connect(
		&m_taginfo_watcher,
		&decltype( m_taginfo_watcher )::finished,
		this,
		&TagNode::gotName,
		Qt::SingleShotConnection );
}

void TagNode::gotName()
{
	const auto result { m_taginfo_watcher.future().result() };
	this->tag_text = QString::number( m_id ) + ": " + result.toQString();

	update();
}

QVariant TagNode::itemChange( GraphicsItemChange change, const QVariant& value )
{
	switch ( change )
	{
		case ItemPositionHasChanged:
			{
				for ( const auto relationship : std::as_const( m_relationships ) )
				{
					relationship->adjust();
				}
				graph->itemMoved();
			}
		default:
			break;
	}

	return QGraphicsItem::itemChange( change, value );
}
