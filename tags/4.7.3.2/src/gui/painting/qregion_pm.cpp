/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Copyright (C) 2010 netlabs.org. OS/2 parts.
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qbitmap.h"
#include "qbuffer.h"
#include "qimage.h"
#include "qpolygon.h"
#include "qregion.h"

#include "qt_os2.h"

QT_BEGIN_NAMESPACE

//  To compensate the difference between Qt (where y axis goes downwards) and
//  GPI (where y axis goes upwards) coordinate spaces when dealing with regions
//  we use the following technique: when a GPI resource is allocated for a Qt
//  region, we simply change the sign of all y coordinates to quickly flip it
//  top to bottom in a manner that doesn't depend on the target device height.
//  All we have to do to apply the created GPI region to a particular GPI device
//  is to align its y axis to the top of the device (i.e. offset the region
//  up by the height of the device), and unalign it afterwards to bring it back
//  to the coordinate space of other device-independent (unaligned) regions.
//  To optimize this, we remember (in data->hgt) the last height value used to
//  align the region, and align it again only if the target device height
//  changes. Zero height indicates a device-independent target (such as other
//  unaligned Qt region).
//
//  The handle() function, used for external access to the region, takes an
//  argument that must be always set to the height of the target device to
//  guarantee the correct y axis alignment.

QRegion::QRegionData QRegion::shared_empty = { Q_BASIC_ATOMIC_INITIALIZER(1),
                                               NULLHANDLE, 0 };

/*!
    \internal

    Deletes the given region handle.
 */
void QRegion::disposeHandle(HRGN hrgn)
{
    if (hrgn != 0)
        GpiDestroyRegion(qt_display_ps(), hrgn);
}

/*!
    \internal

    Updates the region handle so that it is suitable for selection to
    a device with the given \a height.
 */
void QRegion::updateHandle(int targetHeight) const
{
    QRegion *self = const_cast<QRegion*>(this); // we're const here

    if (d->rgn != 0) {
        if (self->d->ref == 1) {
            // align region y axis to the top of the device
            POINTL ptl = { 0, targetHeight - d->height };
            GpiOffsetRegion(qt_display_ps(), d->rgn, &ptl);
            self->d->height = targetHeight;
            return;
        }
        // create a copy since the hande may be in use (this will reset d->rgn)
        *self = copy();
    }

    Q_ASSERT(d->rgn == 0);

    // new/updated/copied Qt region data, create a new HRGN for it
    self->d->height = targetHeight;
    if (d->qt_rgn) {
        if (d->qt_rgn->numRects > 0) {
            if (d->qt_rgn->numRects == 1) {
                // d->qt_rgn->rects is empty, use d->qt_rgn->extents instead
                const QRect r = d->qt_rgn->extents;
                RECTL rcl = { r.left(), d->height - (r.bottom() + 1),
                              r.right() + 1, d->height - r.top() };
                self->d->rgn = GpiCreateRegion(qt_display_ps(), 1, &rcl);
            } else {
                PRECTL rcls = new RECTL[d->qt_rgn->numRects];
                for (int i = 0; i < d->qt_rgn->numRects; ++i) {
                    QRect r = d->qt_rgn->rects.at(i);
                    // note RECTL is inclusive-exclusive here
                    rcls[i].xLeft = r.left();
                    rcls[i].yBottom = d->height - (r.bottom() + 1);
                    rcls[i].xRight = r.right() + 1;
                    rcls[i].yTop = d->height - r.top();
                }
                self->d->rgn = GpiCreateRegion(qt_display_ps(),
                                               d->qt_rgn->numRects, rcls);
                delete[] rcls;
            }
            return;
        }
    }

    // create an empty region
    self->d->rgn = GpiCreateRegion(qt_display_ps(), 0, NULL);
}

QT_END_NAMESPACE
