//
// Created by kj16609 on 5/3/25.
//
#include "TagRelationship.hpp"

#include "TagNode.hpp"

TagRelationship::TagRelationship( TagNode* left, TagNode* right, const RelationshipType relationship ) :
  m_relationship( relationship ),
  m_left( left ),
  m_right( right )
{
	setZValue( -1 ); // Draw behind nodes
	m_left->addRelationship( this );
	m_right->addRelationship( this );
	adjust();
}

QPointF intersects( const QRectF& rect, const QLineF& line )
{
	const auto top_left { rect.topLeft() };
	const auto bottom_right { rect.bottomRight() };

	const auto top_right { QPointF( bottom_right.x(), top_left.y() ) };
	const auto bottom_left { QPointF( top_left.x(), bottom_right.y() ) };

	const QLineF top_line { top_left, top_right };
	const QLineF bottom_line { bottom_left, bottom_right };
	const QLineF left_line { top_left, bottom_left };
	const QLineF right_line { top_right, bottom_right };

	QPointF closest_intersection { line.p2() };
	QPointF intersection { line.p2() };

	const auto isCloser = [ &line, &closest_intersection ]( QPointF point )
	{
		const QLineF line_to_point { line.p1(), point };
		const QLineF line_to_prev_point { line.p1(), closest_intersection };

		return line_to_point.length() < line_to_prev_point.length();
	};

	if ( line.intersects( top_line, &intersection ) == QLineF::BoundedIntersection && isCloser( intersection ) )
		closest_intersection = intersection;

	if ( line.intersects( bottom_line, &intersection ) == QLineF::BoundedIntersection && isCloser( intersection ) )
		closest_intersection = intersection;

	if ( line.intersects( left_line, &intersection ) == QLineF::BoundedIntersection && isCloser( intersection ) )
		closest_intersection = intersection;

	if ( line.intersects( right_line, &intersection ) == QLineF::BoundedIntersection && isCloser( intersection ) )
		closest_intersection = intersection;

	return closest_intersection;
}

void TagRelationship::adjust()
{
	const QLineF line { mapFromItem( m_left, 0, 0 ), mapFromItem( m_right, 0, 0 ) };
	const qreal length { line.length() };

	const auto left_bounds { m_left->sceneBoundingRect() };
	const auto right_bounds { m_right->sceneBoundingRect() };

	prepareGeometryChange();

	if ( length > qreal( 20.0 ) )
	{
		QPointF edge_offset { ( line.dx() * 10 ) / length, ( line.dy() * 10 ) / length };
		// source_point = line.p1() + edge_offset;
		// target_point = line.p2() - edge_offset;

		// source_point = intersects( left_bounds, line );
		source_point = intersects( left_bounds, line );
		target_point = intersects( right_bounds, line );
	}
	else
	{
		source_point = line.p1();
		target_point = line.p1();
	}
}

QRectF TagRelationship::boundingRect() const
{
	constexpr qreal pen_width { 1.0 };
	const qreal extra { ( pen_width + arrow_size ) / 2.0f };

	return QRectF( source_point, QSizeF( target_point.x() - source_point.x(), target_point.y() - source_point.y() ) )
	    .normalized()
	    .adjusted( -extra, -extra, extra, extra );
}

void TagRelationship::paint( QPainter* painter, const QStyleOptionGraphicsItem*, QWidget* )
{
	static constexpr qreal arrow_size { 10.0 };
	static constexpr qreal node_offset { 10.0 };
	QLineF line { source_point, target_point };
	if ( qFuzzyCompare( line.length(), qreal( 0.0 ) ) ) return;

	const QPen alias_pen { Qt::white, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin };
	const QPen parent_pen { Qt::blue, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin };
	const QPen sibling_pen { Qt::red, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin };
	const QPen outline_pen { Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin };

	// Calculate offset target point
	const double angle { std::atan2( -line.dy(), line.dx() ) };
	const QPointF offset_target { target_point
		                          - QPointF( std::cos( angle ) * node_offset, -std::sin( angle ) * node_offset ) };
	QLineF offset_line { source_point, offset_target };

	// Draw black outline
	painter->setPen( outline_pen );
	painter->drawLine( offset_line );

	switch ( m_relationship )
	{
		case ParentChild:
			painter->setPen( parent_pen );
			break;
		case Sibling:
			painter->setPen( sibling_pen );
			break;
		default:
		[[fallthrough]]
		case Aliased:
			painter->setPen( alias_pen );
			break;
	}

	painter->drawLine( offset_line );

	QPointF sourceArrowP1 { source_point
		                    + QPointF( sin( angle + M_PI / 3 ) * arrow_size, cos( angle + M_PI / 3 ) * arrow_size ) };
	QPointF sourceArrowP2 {
		source_point
		+ QPointF( sin( angle + M_PI - M_PI / 3 ) * arrow_size, cos( angle + M_PI - M_PI / 3 ) * arrow_size )
	};

	QPointF destArrowP1 { target_point
		                  + QPointF( sin( angle - M_PI / 3 ) * arrow_size, cos( angle - M_PI / 3 ) * arrow_size ) };
	QPointF destArrowP2 {
		target_point
		+ QPointF( sin( angle - M_PI + M_PI / 3 ) * arrow_size, cos( angle - M_PI + M_PI / 3 ) * arrow_size )
	};

	painter->setBrush( Qt::black );
	// painter->drawPolygon( QPolygonF() << line.p1() << sourceArrowP1 << sourceArrowP2 );
	painter->drawPolygon( QPolygonF() << line.p2() << destArrowP1 << destArrowP2 );
}