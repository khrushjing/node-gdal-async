/******************************************************************************
 *
 * Project:  OGR/DODS Interface
 * Purpose:  Implements OGRDODSGridLayer class, which implements the
 *           "Grid/Array" access strategy.
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

#include "cpl_conv.h"
#include "ogr_dods.h"
#include "cpl_string.h"

CPL_CVSID("$Id: ogrdodsgrid.cpp  $")

/************************************************************************/
/*                          OGRDODSGridLayer()                          */
/************************************************************************/

OGRDODSGridLayer::OGRDODSGridLayer( OGRDODSDataSource *poDSIn,
                                    const char *pszTargetIn,
                                    AttrTable *poOGRLayerInfoIn ) :
    OGRDODSLayer( poDSIn, pszTargetIn, poOGRLayerInfoIn ),
    poTargetGrid(nullptr),
    poTargetArray(nullptr),
    nArrayRefCount(0),
    paoArrayRefs(nullptr),
    nDimCount(0),
    paoDimensions(nullptr),
    nMaxRawIndex(0)
{
/* -------------------------------------------------------------------- */
/*      What is the layer name?                                         */
/* -------------------------------------------------------------------- */
    string oLayerName;
    const char *pszLayerName = pszTargetIn;

    if( poOGRLayerInfo != nullptr )
    {
        oLayerName = poOGRLayerInfo->get_attr( "layer_name" );
        if( strlen(oLayerName.c_str()) > 0 )
            pszLayerName = oLayerName.c_str();
    }

    poFeatureDefn = new OGRFeatureDefn( pszLayerName );
    poFeatureDefn->Reference();

/* -------------------------------------------------------------------- */
/*      Fetch the target variable.                                      */
/* -------------------------------------------------------------------- */
    BaseType *poTargVar = poDS->poDDS->var( pszTargetIn );

    if( poTargVar->type() == dods_grid_c )
    {
        poTargetGrid = dynamic_cast<Grid *>( poTargVar );
        if( poTargetGrid )
            poTargetArray = dynamic_cast<Array *>(poTargetGrid->array_var());
    }
    else if( poTargVar->type() == dods_array_c )
    {
        poTargetGrid = nullptr;
        poTargetArray = dynamic_cast<Array *>( poTargVar );
    }

    if( !poTargetArray )
    {
        CPLAssert( false );
        return;
    }

/* -------------------------------------------------------------------- */
/*      Count arrays in use.                                            */
/* -------------------------------------------------------------------- */
    AttrTable *poExtraContainers = nullptr;
    nArrayRefCount = 1; // primary target.

    if( poOGRLayerInfo != nullptr )
        poExtraContainers = poOGRLayerInfo->find_container("extra_containers");

    if( poExtraContainers != nullptr )
    {
        AttrTable::Attr_iter dv_i;

        for( dv_i = poExtraContainers->attr_begin();
             dv_i != poExtraContainers->attr_end(); dv_i++ )
        {
            nArrayRefCount++;
        }
    }

/* -------------------------------------------------------------------- */
/*      Collect extra_containers.                                       */
/* -------------------------------------------------------------------- */
    paoArrayRefs = new OGRDODSArrayRef[nArrayRefCount];
    paoArrayRefs[0].pszName = CPLStrdup( pszTargetIn );
    paoArrayRefs[0].poArray = poTargetArray;

    nArrayRefCount = 1;

    if( poExtraContainers != nullptr )
    {
        AttrTable::Attr_iter dv_i;

        for( dv_i = poExtraContainers->attr_begin();
             dv_i != poExtraContainers->attr_end(); dv_i++ )
        {
            auto osTargetName(poExtraContainers->get_attr(dv_i));
            const char *pszTargetName = osTargetName.c_str();
            BaseType *poExtraTarget = poDS->poDDS->var( pszTargetName );

            if( poExtraTarget == nullptr )
            {
                CPLError( CE_Warning, CPLE_AppDefined,
                          "Unable to find extra_container '%s', skipping.",
                          pszTargetName );
                continue;
            }

            if( poExtraTarget->type() == dods_array_c )
                paoArrayRefs[nArrayRefCount].poArray =
                    dynamic_cast<Array *>( poExtraTarget );
            else if( poExtraTarget->type() == dods_grid_c )
            {
                Grid *poGrid = dynamic_cast<Grid *>( poExtraTarget );
                paoArrayRefs[nArrayRefCount].poArray =
                    dynamic_cast<Array *>( poGrid->array_var() );
            }
            else
            {
                CPLError( CE_Warning, CPLE_AppDefined,
                          "Target container '%s' is not grid or array, skipping.",
                          pszTargetName );
                continue;
            }

            paoArrayRefs[nArrayRefCount++].pszName = CPLStrdup(pszTargetName);
        }
    }

/* -------------------------------------------------------------------- */
/*      Collect dimension information.                                  */
/* -------------------------------------------------------------------- */
    int iDim;
    Array::Dim_iter iterDim;

    nDimCount = poTargetArray->dimensions();
    paoDimensions = new OGRDODSDim[nDimCount];
    nMaxRawIndex = 1;

    for( iterDim = poTargetArray->dim_begin(), iDim = 0;
         iterDim != poTargetArray->dim_end();
         iterDim++, iDim++ )
    {
        paoDimensions[iDim].pszDimName =
            CPLStrdup(poTargetArray->dimension_name(iterDim).c_str());
        paoDimensions[iDim].nDimStart =
            poTargetArray->dimension_start(iterDim);
        paoDimensions[iDim].nDimEnd =
            poTargetArray->dimension_stop(iterDim);
        paoDimensions[iDim].nDimStride =
            poTargetArray->dimension_stride(iterDim);
        paoDimensions[iDim].poMap = nullptr;

        paoDimensions[iDim].nDimEntries =
            (paoDimensions[iDim].nDimEnd + 1 - paoDimensions[iDim].nDimStart
             + paoDimensions[iDim].nDimStride - 1)
            / paoDimensions[iDim].nDimStride;

        nMaxRawIndex *= paoDimensions[iDim].nDimEntries;
    }

/* -------------------------------------------------------------------- */
/*      If we are working with a grid, collect the maps.                */
/* -------------------------------------------------------------------- */
    if( poTargetGrid != nullptr )
    {
        int iMap;
        Grid::Map_iter iterMap;

        for( iterMap = poTargetGrid->map_begin(), iMap = 0;
             iterMap != poTargetGrid->map_end();
             iterMap++, iMap++ )
        {
            paoDimensions[iMap].poMap = dynamic_cast<Array *>(*iterMap);
        }

        CPLAssert( iMap == nDimCount );
    }

/* -------------------------------------------------------------------- */
/*      Setup field definitions.  The first nDimCount will be the       */
/*      dimension attributes, and after that comes the actual target    */
/*      array.                                                          */
/* -------------------------------------------------------------------- */
    for( iDim = 0; iDim < nDimCount; iDim++ )
    {
        OGRFieldDefn oField( paoDimensions[iDim].pszDimName, OFTInteger );

        if( EQUAL(oField.GetNameRef(), poTargetArray->name().c_str()) )
            oField.SetName(CPLSPrintf("%s_i",paoDimensions[iDim].pszDimName));

        if( paoDimensions[iDim].poMap != nullptr )
        {
            switch( paoDimensions[iDim].poMap->var()->type() )
            {
              case dods_byte_c:
              case dods_int16_c:
              case dods_uint16_c:
              case dods_int32_c:
              case dods_uint32_c:
                oField.SetType( OFTInteger );
                break;

              case dods_float32_c:
              case dods_float64_c:
                oField.SetType( OFTReal );
                break;

              case dods_str_c:
              case dods_url_c:
                oField.SetType( OFTString );
                break;

              default:
                // Ignore
                break;
            }
        }

        poFeatureDefn->AddFieldDefn( &oField );
    }

/* -------------------------------------------------------------------- */
/*      Setup the array attributes themselves.                          */
/* -------------------------------------------------------------------- */
    int iArray;
    for( iArray=0; iArray < nArrayRefCount; iArray++ )
    {
        OGRDODSArrayRef *poRef = paoArrayRefs + iArray;
        OGRFieldDefn oArrayField( poRef->poArray->name().c_str(), OFTInteger );

        switch( poRef->poArray->var()->type() )
        {
          case dods_byte_c:
          case dods_int16_c:
          case dods_uint16_c:
          case dods_int32_c:
          case dods_uint32_c:
            oArrayField.SetType( OFTInteger );
            break;

          case dods_float32_c:
          case dods_float64_c:
            oArrayField.SetType( OFTReal );
            break;

          case dods_str_c:
          case dods_url_c:
            oArrayField.SetType( OFTString );
            break;

          default:
            // Ignore
            break;
        }

        poFeatureDefn->AddFieldDefn( &oArrayField );
        poRef->iFieldIndex = poFeatureDefn->GetFieldCount() - 1;
    }

/* -------------------------------------------------------------------- */
/*      X/Y/Z fields.                                                   */
/* -------------------------------------------------------------------- */
    if( poOGRLayerInfo != nullptr )
    {
        AttrTable *poField = poOGRLayerInfo->find_container("x_field");
        if( poField != nullptr )
        {
            oXField.Initialize( poField );
            oXField.iFieldIndex =
                poFeatureDefn->GetFieldIndex( oXField.pszFieldName );
        }

        poField = poOGRLayerInfo->find_container("y_field");
        if( poField != nullptr )
        {
            oYField.Initialize( poField );
            oYField.iFieldIndex =
                poFeatureDefn->GetFieldIndex( oYField.pszFieldName );
        }

        poField = poOGRLayerInfo->find_container("z_field");
        if( poField != nullptr )
        {
            oZField.Initialize( poField );
            oZField.iFieldIndex =
                poFeatureDefn->GetFieldIndex( oZField.pszFieldName );
        }
    }

/* -------------------------------------------------------------------- */
/*      If we have no layerinfo, then check if there are obvious x/y    */
/*      fields.                                                         */
/* -------------------------------------------------------------------- */
    else
    {
        if( poFeatureDefn->GetFieldIndex( "lat" ) != -1
            && poFeatureDefn->GetFieldIndex( "lon" ) != -1 )
        {
            oXField.Initialize( "lon", "dds" );
            oXField.iFieldIndex = poFeatureDefn->GetFieldIndex( "lon" );
            oYField.Initialize( "lat", "dds" );
            oYField.iFieldIndex = poFeatureDefn->GetFieldIndex( "lat" );
        }
        else if( poFeatureDefn->GetFieldIndex( "latitude" ) != -1
                 && poFeatureDefn->GetFieldIndex( "longitude" ) != -1 )
        {
            oXField.Initialize( "longitude", "dds" );
            oXField.iFieldIndex = poFeatureDefn->GetFieldIndex( "longitude" );
            oYField.Initialize( "latitude", "dds" );
            oYField.iFieldIndex = poFeatureDefn->GetFieldIndex( "latitude" );
        }
    }

/* -------------------------------------------------------------------- */
/*      Set the layer geometry type if we have point inputs.            */
/* -------------------------------------------------------------------- */
    if( oZField.iFieldIndex >= 0 )
        poFeatureDefn->SetGeomType( wkbPoint25D );
    else if( oXField.iFieldIndex >= 0 && oYField.iFieldIndex >= 0 )
        poFeatureDefn->SetGeomType( wkbPoint );
    else
        poFeatureDefn->SetGeomType( wkbNone );
}

/************************************************************************/
/*                         ~OGRDODSGridLayer()                          */
/************************************************************************/

OGRDODSGridLayer::~OGRDODSGridLayer()

{
    delete[] paoArrayRefs;
    delete[] paoDimensions;
}

/************************************************************************/
/*                         ArrayEntryToField()                          */
/************************************************************************/

bool OGRDODSGridLayer::ArrayEntryToField( Array *poArray, void *pRawDataIn,
                                          int iArrayIndex,
                                          OGRFeature *poFeature, int iField )

{
    switch( poArray->var()->type() )
    {
      case dods_byte_c:
      {
          GByte *pabyRawData = (GByte *) pRawDataIn;
          poFeature->SetField( iField, pabyRawData[iArrayIndex] );
      }
      break;

      case dods_int16_c:
      {
          GInt16 *panRawData = (GInt16 *) pRawDataIn;
          poFeature->SetField( iField, panRawData[iArrayIndex] );
      }
      break;

      case dods_uint16_c:
      {
          GUInt16 *panRawData = (GUInt16 *) pRawDataIn;
          poFeature->SetField( iField, panRawData[iArrayIndex] );
      }
      break;

      case dods_int32_c:
      {
          GInt32 *panRawData = (GInt32 *) pRawDataIn;
          poFeature->SetField( iField, panRawData[iArrayIndex] );
      }
      break;

      case dods_uint32_c:
      {
          GUInt32 *panRawData = (GUInt32 *) pRawDataIn;
          poFeature->SetField( iField, (int) panRawData[iArrayIndex] );
      }
      break;

      case dods_float32_c:
      {
          float * pafRawData = (float *) pRawDataIn;
          poFeature->SetField( iField, pafRawData[iArrayIndex] );
      }
      break;

      case dods_float64_c:
      {
          double * padfRawData = (double *) pRawDataIn;
          poFeature->SetField( iField, padfRawData[iArrayIndex] );
      }
      break;

      default:
        return false;
    }

    return true;
}

/************************************************************************/
/*                             GetFeature()                             */
/************************************************************************/

OGRFeature *OGRDODSGridLayer::GetFeature( GIntBig nFeatureId )

{
    if( nFeatureId < 0 || nFeatureId >= nMaxRawIndex )
        return nullptr;

/* -------------------------------------------------------------------- */
/*      Ensure we have the dataset.                                     */
/* -------------------------------------------------------------------- */
    if( !ProvideDataDDS() )
        return nullptr;

/* -------------------------------------------------------------------- */
/*      Create the feature being read.                                  */
/* -------------------------------------------------------------------- */
    OGRFeature *poFeature = new OGRFeature( poFeatureDefn );
    poFeature->SetFID( nFeatureId );
    m_nFeaturesRead++;

/* -------------------------------------------------------------------- */
/*      Establish the values for the various dimension indices.         */
/* -------------------------------------------------------------------- */
    int iDim;
    int nRemainder = static_cast<int>(nFeatureId);

    for( iDim = nDimCount-1; iDim >= 0; iDim-- )
    {
        paoDimensions[iDim].iLastValue =
            (nRemainder % paoDimensions[iDim].nDimEntries)
            * paoDimensions[iDim].nDimStride
            + paoDimensions[iDim].nDimStart;
        nRemainder = nRemainder / paoDimensions[iDim].nDimEntries;

        if( poTargetGrid == nullptr )
            poFeature->SetField( iDim, paoDimensions[iDim].iLastValue );
    }
    CPLAssert( nRemainder == 0 );

/* -------------------------------------------------------------------- */
/*      For grids, we need to apply the values of the dimensions        */
/*      looked up in the corresponding map.                             */
/* -------------------------------------------------------------------- */
    if( poTargetGrid != nullptr )
    {
        for( iDim = 0; iDim < nDimCount; iDim++ )
        {
            ArrayEntryToField( paoDimensions[iDim].poMap,
                               paoDimensions[iDim].pRawData,
                               paoDimensions[iDim].iLastValue,
                               poFeature, iDim );
        }
    }

/* -------------------------------------------------------------------- */
/*      Process all the regular data fields.                            */
/* -------------------------------------------------------------------- */
    int iArray;
    for( iArray = 0; iArray < nArrayRefCount; iArray++ )
    {
        OGRDODSArrayRef *poRef = paoArrayRefs + iArray;

        ArrayEntryToField( poRef->poArray, poRef->pRawData,
                           static_cast<int>(nFeatureId),
                           poFeature, poRef->iFieldIndex );
    }

/* -------------------------------------------------------------------- */
/*      Do we have geometry information?                                */
/* -------------------------------------------------------------------- */
    if( oXField.iFieldIndex >= 0 && oYField.iFieldIndex >= 0 )
    {
        OGRPoint *poPoint = new OGRPoint();

        poPoint->setX( poFeature->GetFieldAsDouble( oXField.iFieldIndex ) );
        poPoint->setY( poFeature->GetFieldAsDouble( oYField.iFieldIndex ) );

        if( oZField.iFieldIndex >= 0 )
            poPoint->setZ( poFeature->GetFieldAsDouble(oZField.iFieldIndex) );

        poFeature->SetGeometryDirectly( poPoint );
    }

    return poFeature;
}

/************************************************************************/
/*                           ProvideDataDDS()                           */
/************************************************************************/

bool OGRDODSGridLayer::ProvideDataDDS()

{
    if( bDataLoaded )
        return poTargetVar != nullptr;

    const bool  bResult = OGRDODSLayer::ProvideDataDDS();

    if( !bResult )
        return bResult;

    for( int iArray=0; iArray < nArrayRefCount; iArray++ )
    {
        OGRDODSArrayRef *poRef = paoArrayRefs + iArray;
        BaseType *poTarget = poDataDDS->var( poRef->pszName );

        // Reset ref array pointer to point in DataDDS result.
        poRef->poArray = nullptr;
        if( poTarget->type() == dods_grid_c )
        {
            Grid *poGrid = dynamic_cast<Grid *>( poTarget );
            if( poGrid )
                poRef->poArray = dynamic_cast<Array *>(poGrid->array_var());

            if( iArray == 0 )
                poTargetGrid = poGrid;
        }
        else if( poTarget->type() == dods_array_c )
        {
            poRef->poArray = dynamic_cast<Array *>( poTarget );
        }

        if( !(poRef->poArray) )
        {
            CPLAssert( false );
            return false;
        }

        if( iArray == 0 )
            poTargetArray = poRef->poArray;

        // Allocate appropriate raw data array, and pull out data into it.
        poRef->pRawData = CPLMalloc( poRef->poArray->width() );
        poRef->poArray->buf2val( &(poRef->pRawData) );
    }

    // Setup pointers to each of the map objects.
    if( poTargetGrid != nullptr )
    {
        int iMap = 0;
        Grid::Map_iter iterMap;

        for( iterMap = poTargetGrid->map_begin();
             iterMap != poTargetGrid->map_end();
             iterMap++, iMap++ )
        {
            paoDimensions[iMap].poMap = dynamic_cast<Array *>(*iterMap);
            if( paoDimensions[iMap].poMap == nullptr )
                return false;
            paoDimensions[iMap].pRawData =
                CPLMalloc( paoDimensions[iMap].poMap->width() );
            paoDimensions[iMap].poMap->buf2val( &(paoDimensions[iMap].pRawData) );
        }
    }

    return bResult;
}

/************************************************************************/
/*                          GetFeatureCount()                           */
/************************************************************************/

GIntBig OGRDODSGridLayer::GetFeatureCount( int bForce )

{
    if( m_poFilterGeom == nullptr && m_poAttrQuery == nullptr )
        return nMaxRawIndex;
    else
        return OGRDODSLayer::GetFeatureCount( bForce );
}
