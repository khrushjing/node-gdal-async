/*****************************************************************************
 *
 * Project:  DB2 Spatial driver
 * Purpose:  Implements OGRDB2GeometryValidator class to create valid
 *           Geometries.
 * Author:   David Adler, dadler at adtechgeospatial dot com
 *
 *****************************************************************************
 * Copyright (c) 2010, Tamas Szekeres
 * Copyright (c) 2015, David Adler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "cpl_conv.h"
#include "ogr_db2.h"

CPL_CVSID("$Id: ogrdb2geometryvalidator.cpp  $")

/************************************************************************/
/*                   OGRDB2GeometryValidator()                        */
/************************************************************************/

OGRDB2GeometryValidator::OGRDB2GeometryValidator(OGRGeometry *poGeom)
{
    poOriginalGeometry = poGeom;
    poValidGeometry = NULL;
    bIsValid = ValidateGeometry(poGeom);
}

/************************************************************************/
/*                      ~OGRDB2GeometryValidator()                    */
/************************************************************************/

OGRDB2GeometryValidator::~OGRDB2GeometryValidator()
{
    if (poValidGeometry)
        delete poValidGeometry;
}

/************************************************************************/
/*                         ValidatePoint()                              */
/************************************************************************/

int OGRDB2GeometryValidator::ValidatePoint(CPL_UNUSED OGRPoint* poGeom)
{
    return TRUE;
}

/************************************************************************/
/*                     ValidateMultiPoint()                             */
/************************************************************************/

int OGRDB2GeometryValidator::ValidateMultiPoint(
                                        CPL_UNUSED OGRMultiPoint* poGeom)
{
    return TRUE;
}

/************************************************************************/
/*                         ValidateLineString()                         */
/************************************************************************/

int OGRDB2GeometryValidator::ValidateLineString(OGRLineString * poGeom)
{
    OGRPoint* poPoint0 = NULL;
    int i;
    int bResult = FALSE;

    for (i = 0; i < poGeom->getNumPoints(); i++)
    {
        if (poPoint0 == NULL)
        {
            poPoint0 = new OGRPoint();
            poGeom->getPoint(i, poPoint0);
            continue;
        }

        if (poPoint0->getX() == poGeom->getX(i)
            && poPoint0->getY() == poGeom->getY(i))
            continue;

        bResult = TRUE;
        break;
    }

    if (!bResult)
    {
        if (poValidGeometry)
            delete poValidGeometry;

        poValidGeometry = NULL;

        // create a compatible geometry
        if (poPoint0 != NULL)
        {
            CPLError( CE_Warning, CPLE_NotSupported,
                      "Linestring has no distinct points "
                      "constructing point geometry instead." );

            // create a point
            poValidGeometry = poPoint0;
            poPoint0 = NULL;
        }
        else
        {
            CPLError( CE_Warning, CPLE_NotSupported,
                      "Linestring has no points. Removing the geometry "
                      "from the output." );
        }
    }

    if (poPoint0)
        delete poPoint0;

    return bResult;
}

/************************************************************************/
/*                         ValidateLinearRing()                         */
/************************************************************************/

int OGRDB2GeometryValidator::ValidateLinearRing(OGRLinearRing * poGeom)
{
    OGRPoint* poPoint0 = NULL;
    OGRPoint* poPoint1 = NULL;
    int i;
    int bResult = FALSE;

    poGeom->closeRings();

    for (i = 0; i < poGeom->getNumPoints(); i++)
    {
        if (poPoint0 == NULL)
        {
            poPoint0 = new OGRPoint();
            poGeom->getPoint(i, poPoint0);
            continue;
        }

        if (poPoint0->getX() == poGeom->getX(i)
            && poPoint0->getY() == poGeom->getY(i))
            continue;

        if (poPoint1 == NULL)
        {
            poPoint1 = new OGRPoint();
            poGeom->getPoint(i, poPoint1);
            continue;
        }

        if (poPoint1->getX() == poGeom->getX(i)
            && poPoint1->getY() == poGeom->getY(i))
            continue;

        bResult = TRUE;
        break;
    }

    if (!bResult)
    {
        if (poValidGeometry)
            delete poValidGeometry;

        poValidGeometry = NULL;

        // create a compatible geometry
        if (poPoint1 != NULL)
        {
            CPLError( CE_Warning, CPLE_NotSupported,
                      "Linear ring has only 2 distinct points constructing "
                      "linestring geometry instead." );

            // create a linestring
            poValidGeometry = new OGRLineString();
            ((OGRLineString*)poValidGeometry)->setNumPoints( 2 );
            ((OGRLineString*)poValidGeometry)->addPoint(poPoint0);
            ((OGRLineString*)poValidGeometry)->addPoint(poPoint1);
        }
        else if (poPoint0 != NULL)
        {
            CPLError( CE_Warning, CPLE_NotSupported,
                      "Linear ring has no distinct points constructing point "
                      "geometry instead." );

            // create a point
            poValidGeometry = poPoint0;
            poPoint0 = NULL;
        }
        else
        {
            CPLError( CE_Warning, CPLE_NotSupported,
                      "Linear ring has no points. Removing the geometry "
                      "from the output." );
        }
    }

    if (poPoint0)
        delete poPoint0;

    if (poPoint1)
        delete poPoint1;

    return bResult;
}

/************************************************************************/
/*                     ValidateMultiLineString()                        */
/************************************************************************/

int OGRDB2GeometryValidator::ValidateMultiLineString(
                                            OGRMultiLineString * poGeom)
{
    int i, j;
    OGRGeometry* poLineString;
    OGRGeometryCollection* poGeometries = NULL;

    for (i = 0; i < poGeom->getNumGeometries(); i++)
    {
        poLineString = poGeom->getGeometryRef(i);
        if (poLineString->getGeometryType() != wkbLineString
            && poLineString->getGeometryType() != wkbLineString25D)
        {
            // non linestring geometry
            if (!poGeometries)
            {
                poGeometries = new OGRGeometryCollection();
                for (j = 0; j < i; j++)
                    poGeometries->addGeometry(poGeom->getGeometryRef(j));
            }
            if (ValidateGeometry(poLineString))
                poGeometries->addGeometry(poLineString);
            else
                poGeometries->addGeometry(poValidGeometry);

            continue;
        }

        if (!ValidateLineString((OGRLineString*)poLineString))
        {
            // non valid linestring
            if (!poGeometries)
            {
                poGeometries = new OGRGeometryCollection();
                for (j = 0; j < i; j++)
                    poGeometries->addGeometry(poGeom->getGeometryRef(j));
            }

            poGeometries->addGeometry(poValidGeometry);
            continue;
        }

        if (poGeometries)
            poGeometries->addGeometry(poLineString);
    }

    if (poGeometries)
    {
        if (poValidGeometry)
            delete poValidGeometry;

        poValidGeometry = poGeometries;
    }

    return poValidGeometry == NULL;
}

/************************************************************************/
/*                         ValidatePolygon()                            */
/************************************************************************/

int OGRDB2GeometryValidator::ValidatePolygon(OGRPolygon* poGeom)
{
    int i,j;
    OGRLinearRing* poRing = poGeom->getExteriorRing();
    OGRGeometry* poInteriorRing;

    if (poRing == NULL)
        return FALSE;

    OGRGeometryCollection* poGeometries = NULL;

    if (!ValidateLinearRing(poRing))
    {
        if (poGeom->getNumInteriorRings() > 0)
        {
            poGeometries = new OGRGeometryCollection();
            poGeometries->addGeometryDirectly(poValidGeometry);
        }
    }

    for (i = 0; i < poGeom->getNumInteriorRings(); i++)
    {
        poInteriorRing = poGeom->getInteriorRing(i);
        if (!ValidateLinearRing((OGRLinearRing*)poInteriorRing))
        {
            if (!poGeometries)
            {
                poGeometries = new OGRGeometryCollection();
                poGeometries->addGeometry(poRing);
                for (j = 0; j < i; j++)
                    poGeometries->addGeometry(poGeom->getInteriorRing(j));
            }

            poGeometries->addGeometry(poValidGeometry);
            continue;
        }

        if (poGeometries)
            poGeometries->addGeometry(poInteriorRing);
    }

    if (poGeometries)
    {
        if (poValidGeometry)
            delete poValidGeometry;

        poValidGeometry = poGeometries;
    }

    return poValidGeometry == NULL;
}

/************************************************************************/
/*                         ValidateMultiPolygon()                       */
/************************************************************************/

int OGRDB2GeometryValidator::ValidateMultiPolygon(OGRMultiPolygon* poGeom)
{
    int i, j;
    OGRGeometry* poPolygon;
    OGRGeometryCollection* poGeometries = NULL;

    for (i = 0; i < poGeom->getNumGeometries(); i++)
    {
        poPolygon = poGeom->getGeometryRef(i);
        if (poPolygon->getGeometryType() != wkbPolygon
            && poPolygon->getGeometryType() != wkbPolygon25D)
        {
            // non polygon geometry
            if (!poGeometries)
            {
                poGeometries = new OGRGeometryCollection();
                for (j = 0; j < i; j++)
                    poGeometries->addGeometry(poGeom->getGeometryRef(j));
            }
            if (ValidateGeometry(poPolygon))
                poGeometries->addGeometry(poPolygon);
            else
                poGeometries->addGeometry(poValidGeometry);

            continue;
        }

        if (!ValidatePolygon(poPolygon->toPolygon()))
        {
            // non valid polygon
            if (!poGeometries)
            {
                poGeometries = new OGRGeometryCollection();
                for (j = 0; j < i; j++)
                    poGeometries->addGeometry(poGeom->getGeometryRef(j));
            }

            poGeometries->addGeometry(poValidGeometry);
            continue;
        }

        if (poGeometries)
            poGeometries->addGeometry(poPolygon);
    }

    if (poGeometries)
    {
        if (poValidGeometry)
            delete poValidGeometry;

        poValidGeometry = poGeometries;
    }

    return poValidGeometry == NULL;
}

/************************************************************************/
/*                     ValidateGeometryCollection()                     */
/************************************************************************/

int OGRDB2GeometryValidator::ValidateGeometryCollection(
                                            OGRGeometryCollection* poGeom)
{
    int i, j;
    OGRGeometry* poGeometry;
    OGRGeometryCollection* poGeometries = NULL;

    for (i = 0; i < poGeom->getNumGeometries(); i++)
    {
        poGeometry = poGeom->getGeometryRef(i);

        if (!ValidateGeometry(poGeometry))
        {
            // non valid geometry
            if (!poGeometries)
            {
                poGeometries = new OGRGeometryCollection();
                for (j = 0; j < i; j++)
                    poGeometries->addGeometry(poGeom->getGeometryRef(j));
            }

            if (poValidGeometry)
                poGeometries->addGeometry(poValidGeometry);
            continue;
        }

        if (poGeometries)
            poGeometries->addGeometry(poGeometry);
    }

    if (poGeometries)
    {
        if (poValidGeometry)
            delete poValidGeometry;

        poValidGeometry = poGeometries;
    }

    return poValidGeometry == NULL;
}

/************************************************************************/
/*                         ValidateGeometry()                           */
/************************************************************************/

int OGRDB2GeometryValidator::ValidateGeometry(OGRGeometry* poGeom)
{
    if (!poGeom)
        return FALSE;

    switch (poGeom->getGeometryType())
    {
    case wkbPoint:
    case wkbPoint25D:
        return ValidatePoint(poGeom->toPoint());
    case wkbLineString:
    case wkbLineString25D:
        return ValidateLineString(poGeom->toLineString());
    case wkbPolygon:
    case wkbPolygon25D:
        return ValidatePolygon(poGeom->toPolygon());
    case wkbMultiPoint:
    case wkbMultiPoint25D:
        return ValidateMultiPoint(poGeom->toMultiPoint());
    case wkbMultiLineString:
    case wkbMultiLineString25D:
        return ValidateMultiLineString(poGeom->toMultiLineString());
    case wkbMultiPolygon:
    case wkbMultiPolygon25D:
        return ValidateMultiPolygon(poGeom->toMultiPolygon());
    case wkbGeometryCollection:
    case wkbGeometryCollection25D:
        return ValidateGeometryCollection(poGeom->toGeometryCollection());
    case wkbLinearRing:
        return ValidateLinearRing(poGeom->toLinearRing());
    default:
        return FALSE;
    }
}

/************************************************************************/
/*                      GetValidGeometryRef()                           */
/************************************************************************/
OGRGeometry* OGRDB2GeometryValidator::GetValidGeometryRef()
{
    if (bIsValid || poOriginalGeometry == NULL)
        return poOriginalGeometry;

    if (poValidGeometry)
    {
        CPLError( CE_Warning, CPLE_NotSupported,
                  "Invalid geometry has been converted from %s to %s.",
                  poOriginalGeometry->getGeometryName(),
                  poValidGeometry->getGeometryName() );
    }
    else
    {
        CPLError( CE_Warning, CPLE_NotSupported,
                  "Invalid geometry has been converted from %s to null.",
                  poOriginalGeometry->getGeometryName());
    }

    return poValidGeometry;
}
