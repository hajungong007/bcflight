/*
 * BCFlight
 * Copyright (C) 2016 Adrien Aubry (drich)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <QtGui/QPainter>
#include <QtCore/QDebug>
#include "HStatusBar.h"

HStatusBar::HStatusBar( QWidget* parent )
	: QWidget( parent )
	, mValue( 50 )
	, mMaxValue( 100 )
{
}


HStatusBar::~HStatusBar()
{
}


void HStatusBar::setValue( int32_t v )
{
	mValue = v;
	repaint();
}


void HStatusBar::setMaxValue( int32_t v )
{
	mMaxValue = v;
}


void HStatusBar::setSuffix( const QString& sfx )
{
	mSuffix = sfx;
}


void HStatusBar::paintEvent( QPaintEvent* ev )
{
	
	QPainter painter( this );

	QPainterPath backpath;
	backpath.addRoundedRect( QRectF( 0, 0, width() - 1, height() - 1 ), 2, 2 );
	painter.fillPath( backpath, QColor( 32, 32, 38 ) );
	painter.setPen( Qt::darkGray );
	painter.drawPath( backpath );

	QPainterPath path;
	path.addRoundedRect( QRectF( 1, 1, mValue * ( width() - 2 ) / mMaxValue, height() - 2 ), 5, 5 );
	painter.fillPath( path, QColor( 0, 64, 128 ) );

	painter.setFont( QFont( "Helvetica", height() / 2 ) );
	painter.setPen( Qt::white );
	painter.drawText( QRect( 0, 0, width(), height() ), Qt::AlignCenter, QString::number( mValue ) + mSuffix );
}
