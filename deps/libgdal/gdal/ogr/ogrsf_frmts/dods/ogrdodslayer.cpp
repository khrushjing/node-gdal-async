/******************************************************************************
 *
 * Project:  OGR/DODS Interface
 * Purpose:  Implements OGRDODSLayer class.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2004, Frank Warmerdam <warmerdam@pobox.com>
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

#include "ogr_dods.h"
#include "cpl_conv.h"

CPL_CVSID("$Id: ogrdodslayer.cpp  $")

/************************************************************************/
/*                            OGRDODSLayer()                            */
/************************************************************************/

OGRDODSLayer::OGRDODSLayer( OGRDODSDataSource *poDSIn,
                            const char *pszTargetIn,
                            AttrTable *poOGRLayerInfoIn ) :
    poFeatureDefn(nullptr),
    poSRS(nullptr),
    iNextShapeId(0),
    poDS(poDSIn),
    pszQuery(nullptr),
    pszFIDColumn (nullptr),
    pszTarget(CPLStrdup( pszTargetIn )),
    papoFields(nullptr),
    bDataLoaded(false),
    poConnection(nullptr),
    poDataDDS(new DataDDS( poDSIn->poBTF )),
    poTargetVar(nullptr),
    poOGRLayerInfo(poOGRLayerInfoIn),
    bKnowExtent(false)
{
/* ==================================================================== */
/*      Harvest some metadata if available.                             */
/* ==================================================================== */
    if( poOGRLayerInfo != nullptr )
    {
        string oMValue;

/* -------------------------------------------------------------------- */
/*      spatial_ref                                                     */
/* -------------------------------------------------------------------- */
        oMValue = poOGRLayerInfo->get_attr( "spatial_ref" );
        if( oMValue.length() > 0 )
        {
            poSRS = new OGRSpatialReference();
            poSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if( poSRS->SetFromUserInput( oMValue.c_str() ) != OGRERR_NONE )
            {
                CPLError( CE_Warning, CPLE_AppDefined,
                          "Ignoring unrecognized SRS '%s'",
                          oMValue.c_str() );
                delete poSRS;
                poSRS = nullptr;
            }
        }

/* -------------------------------------------------------------------- */
/*      Layer extents.                                                  */
/* -------------------------------------------------------------------- */
        AttrTable *poLayerExt=poOGRLayerInfo->find_container("layer_extents");
        if( poLayerExt != nullptr )
        {
            bKnowExtent = true;
            sExtent.MinX = CPLAtof(poLayerExt->get_attr("x_min").c_str());
            sExtent.MaxX = CPLAtof(poLayerExt->get_attr("x_max").c_str());
            sExtent.MinY = CPLAtof(poLayerExt->get_attr("y_min").c_str());
            sExtent.MaxY = CPLAtof(poLayerExt->get_attr("y_max").c_str());
        }
    }

/* ==================================================================== */
/*      Are we actually referencing a nested subsequence?  If so, we    */
/*      need to find the super sequence so we can do the layered        */
/*      stepping.                                                       */
/* ==================================================================== */
}

/************************************************************************/
/*                           ~OGRDODSLayer()                            */
/************************************************************************/

OGRDODSLayer::~OGRDODSLayer()

{
    if( m_nFeaturesRead > 0 && poFeatureDefn != nullptr )
    {
        CPLDebug( "DODS", "%d features read on layer '%s'.",
                  (int) m_nFeaturesRead,
                  poFeatureDefn->GetName() );
    }

    if( papoFields != nullptr && poFeatureDefn != nullptr )
    {
        int iField;

        for( iField = 0; iField < poFeatureDefn->GetFieldCount(); iField++ )
            delete papoFields[iField];

        CPLFree( papoFields );
    }

    if( poSRS != nullptr )
        poSRS->Release();

    if( poFeatureDefn != nullptr )
        poFeatureDefn->Release();

    CPLFree( pszFIDColumn );
    pszFIDColumn = nullptr;

    CPLFree( pszTarget );
    pszTarget = nullptr;

    if( poConnection != nullptr )
        delete poConnection;

    delete poDataDDS;
}

/************************************************************************/
/*                            ResetReading()                            */
/************************************************************************/

void OGRDODSLayer::ResetReading()

{
    iNextShapeId = 0;
}

/************************************************************************/
/*                           GetNextFeature()                           */
/************************************************************************/

OGRFeature *OGRDODSLayer::GetNextFeature()

{
    for( OGRFeature *poFeature = GetFeature( iNextShapeId++ );
         poFeature != nullptr;
         poFeature = GetFeature( iNextShapeId++ ) )
    {
        if( FilterGeometry( poFeature->GetGeometryRef() )
            && (m_poAttrQuery == nullptr
                || m_poAttrQuery->Evaluate( poFeature )) )
            return poFeature;

        delete poFeature;
    }

    return nullptr;
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRDODSLayer::TestCapability( const char * /* pszCap */ )

{
    return FALSE;
}

/************************************************************************/
/*                           GetSpatialRef()                            */
/************************************************************************/

OGRSpatialReference *OGRDODSLayer::GetSpatialRef()

{
    return poSRS;
}

/************************************************************************/
/*                           ProvideDataDDS()                           */
/************************************************************************/

bool OGRDODSLayer::ProvideDataDDS()

{
    if( bDataLoaded )
        return poTargetVar != nullptr;

    bDataLoaded = true;
    try
    {
        poConnection = new AISConnect( poDS->oBaseURL );
        CPLDebug( "DODS", "request_data(%s,%s)",
                  poDS->oBaseURL.c_str(),
                  (poDS->oProjection + poDS->oConstraints).c_str() );

        // We may need to use custom constraints here.
        poConnection->request_data( *poDataDDS,
                                    poDS->oProjection + poDS->oConstraints );
    }
    catch (Error &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "DataDDS request failed:\n%s",
                  e.get_error_message().c_str() );
        return false;
    }

    poTargetVar = poDataDDS->var( pszTarget );

    return poTargetVar != nullptr;
}

/************************************************************************/
/*                             GetExtent()                              */
/************************************************************************/

OGRErr OGRDODSLayer::GetExtent(OGREnvelope *psExtent, int bForce)

{
    if( bKnowExtent )
    {
        *psExtent = sExtent;
        return OGRERR_NONE;
    }

    if( !bForce )
        return OGRERR_FAILURE;

    const OGRErr eErr = OGRLayer::GetExtent( &sExtent, bForce );
    if( eErr == OGRERR_NONE )
    {
        bKnowExtent = true;
        *psExtent = sExtent;
    }

    return eErr;
}
