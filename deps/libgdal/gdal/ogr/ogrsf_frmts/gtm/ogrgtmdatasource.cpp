/******************************************************************************
 *
 * Project:  GTM Driver
 * Purpose:  Implementation of OGRGTMDataSource class.
 * Author:   Leonardo de Paula Rosa Piga; http://lampiao.lsc.ic.unicamp.br/~piga
 *
 ******************************************************************************
 * Copyright (c) 2009, Leonardo de Paula Rosa Piga
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

#include "ogr_gtm.h"

#include <algorithm>

CPL_CVSID("$Id: ogrgtmdatasource.cpp  $")

/************************************************************************/
/*                         OGRGTMDataSource()                           */
/************************************************************************/

OGRGTMDataSource::OGRGTMDataSource() :
    fpOutput(nullptr),
    fpTmpTrackpoints(nullptr),
    pszTmpTrackpoints(nullptr),
    fpTmpTracks(nullptr),
    pszTmpTracks(nullptr),
    poGTMFile(nullptr),
    pszName(nullptr),
    papoLayers(nullptr),
    nLayers(0),
    bIssuedCTError(false),
    minlat(0),
    maxlat(0),
    minlon(0),
    maxlon(0),
    numWaypoints(0),
    numTracks(0),
    numTrackpoints(0)
{}

/************************************************************************/
/*                       AppendTemporaryFiles()                         */
/************************************************************************/
void OGRGTMDataSource::AppendTemporaryFiles()
{
    if( fpOutput == nullptr )
        return;

    if (numTrackpoints == 0 && numTracks == 0)
        return;

    void* pBuffer = CPLMalloc(2048);

    // Append Trackpoints to the output file
    fpTmpTrackpoints = VSIFOpenL( pszTmpTrackpoints, "r" );
    if (fpTmpTrackpoints != nullptr)
    {
        while ( !VSIFEofL(fpTmpTrackpoints) )
        {
            const size_t bytes = VSIFReadL(pBuffer, 1, 2048, fpTmpTrackpoints);
            VSIFWriteL(pBuffer, bytes, 1, fpOutput);
        }
        VSIFCloseL( fpTmpTrackpoints );
        fpTmpTrackpoints = nullptr;
    }

    // Append Tracks to the output file
    fpTmpTracks = VSIFOpenL( pszTmpTracks, "r" );
    if (fpTmpTracks != nullptr)
    {
        while ( !VSIFEofL(fpTmpTracks) )
        {
            const size_t bytes = VSIFReadL(pBuffer, 1, 2048, fpTmpTracks);
            VSIFWriteL(pBuffer, bytes, 1, fpOutput);
        }
        VSIFCloseL( fpTmpTracks );
        fpTmpTracks = nullptr;
    }
    CPLFree(pBuffer);
}

/************************************************************************/
/*                       WriteWaypointStyles()                          */
/************************************************************************/
void OGRGTMDataSource::WriteWaypointStyles()
{
    if( fpOutput != nullptr )
    {
        // We have waypoints, thus we need to write the default
        // waypoint style as defined by the specification
        if ( numWaypoints != 0)
        {
            void* pBuffer = CPLMalloc(35);
            void* pBufferAux = pBuffer;
            for (int i = 0; i < 4; ++i)
            {
                // height
                appendInt(pBufferAux, -11);
                pBufferAux = ((char*)pBufferAux) + 4;
                // facename size
                appendUShort(pBufferAux, 5);
                pBufferAux = ((char*)pBufferAux) + 2;
                // facename
                memcpy((char*)pBufferAux, "Arial", 5);
                pBufferAux = ((char*)pBufferAux) + 5;
                // dspl
                appendUChar(pBufferAux, (unsigned char) i);
                pBufferAux = ((char*)pBufferAux) + 1;
                // color
                appendInt(pBufferAux, 0);
                pBufferAux = ((char*)pBufferAux) + 4;
                // weight
                appendInt(pBufferAux, 400);
                pBufferAux = ((char*)pBufferAux) + 4;
                // scale1
                appendInt(pBufferAux, 0);
                pBufferAux = ((char*)pBufferAux) + 4;
                // border
                appendUChar(pBufferAux, (i != 3) ? 0 : 139);
                pBufferAux = ((char*)pBufferAux) + 1;
                // background
                appendUShort(pBufferAux, (i != 3) ? 0 : 0xFF);
                pBufferAux = ((char*)pBufferAux) + 2;
                // backcolor
                appendInt(pBufferAux, (i != 3) ? 0 : 0xFFFF);
                pBufferAux = ((char*)pBufferAux) + 4;
                // italic, underline, strikeout
                appendInt(pBufferAux, 0);
                pBufferAux = ((char*)pBufferAux) + 3;
                // alignment
                appendUChar(pBufferAux, (i != 3) ? 0 : 1);
                pBufferAux = pBuffer;
                VSIFWriteL(pBuffer, 35, 1, fpOutput);
            }
            CPLFree(pBuffer);
        }
    }
}

/************************************************************************/
/*                        ~OGRGTMDataSource()                           */
/************************************************************************/

OGRGTMDataSource::~OGRGTMDataSource()
{
    if (fpTmpTrackpoints != nullptr)
        VSIFCloseL( fpTmpTrackpoints );

    if (fpTmpTracks != nullptr)
        VSIFCloseL( fpTmpTracks );

    WriteWaypointStyles();
    AppendTemporaryFiles();

    if( fpOutput != nullptr )
    {
        /* Adjust header counters */
        VSIFSeekL(fpOutput, NWPTS_OFFSET, SEEK_SET);
        writeInt(fpOutput, numWaypoints);
        writeInt(fpOutput, numTrackpoints);

        VSIFSeekL(fpOutput, NTK_OFFSET, SEEK_SET);
        writeInt(fpOutput, numTracks);

        /* Adjust header bounds */
        VSIFSeekL(fpOutput, BOUNDS_OFFSET, SEEK_SET);
        writeFloat(fpOutput, maxlon);
        writeFloat(fpOutput, minlon);
        writeFloat(fpOutput, maxlat);
        writeFloat(fpOutput, minlat);
        VSIFCloseL( fpOutput );
    }

    for( int i = 0; i < nLayers; i++ )
        delete papoLayers[i];
    CPLFree( papoLayers );

    CPLFree( pszName );

    if (pszTmpTracks != nullptr)
    {
        VSIUnlink( pszTmpTracks );
        CPLFree( pszTmpTracks );
    }

    if (pszTmpTrackpoints != nullptr)
    {
        VSIUnlink( pszTmpTrackpoints );
        CPLFree( pszTmpTrackpoints );
    }

    if (poGTMFile != nullptr)
        delete poGTMFile;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

int OGRGTMDataSource::Open(const char* pszFilename, int bUpdate)
{
    CPLAssert( pszFilename != nullptr );

    /* Should not happen as the driver already returned if bUpdate == NULL */
    if (bUpdate)
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "GTM driver does not support opening in update mode");
        return FALSE;
    }

/* -------------------------------------------------------------------- */
/*      Create a GTM object and open the source file.                   */
/* -------------------------------------------------------------------- */
    poGTMFile = new GTM();

    if ( !poGTMFile->Open( pszFilename ) )
    {
        delete poGTMFile;
        poGTMFile = nullptr;
        return FALSE;
    }

/* -------------------------------------------------------------------- */
/*      Validate it by start parsing                                    */
/* -------------------------------------------------------------------- */
    if( !poGTMFile->isValid() )
    {
        delete poGTMFile;
        poGTMFile = nullptr;
        return FALSE;
    }

    pszName = CPLStrdup( pszFilename );
/* -------------------------------------------------------------------- */
/*      Now, we are able to read the file header and find the position  */
/*        of the first waypoint and the position of the first track.      */
/* -------------------------------------------------------------------- */
    if ( !poGTMFile->readHeaderNumbers() )
        return FALSE;

/* -------------------------------------------------------------------- */
/*      We can start reading the file elements                          */
/*      We are going to translate GTM features into layers              */
/* -------------------------------------------------------------------- */
    char* pszBaseFileName = CPLStrdup( CPLGetBasename(pszFilename) );
    /* We are going to create two layers, one for storing waypoints and
       another for storing tracks */
    papoLayers = (OGRGTMLayer **) CPLMalloc(sizeof(void*) * 2);

    /* Create a spatial reference for WGS8*/
    OGRSpatialReference* poSRS = new OGRSpatialReference(nullptr);
    poSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    poSRS->SetWellKnownGeogCS( "WGS84" );

    /* Waypoint layer */
    size_t layerNameSize = strlen(pszBaseFileName) + sizeof("_waypoints");
    char* pszLayerName = (char*) CPLMalloc(layerNameSize);
    /* The layer name will be "<basename>_waypoints" */
    strcpy (pszLayerName, pszBaseFileName);
    CPLStrlcat (pszLayerName, "_waypoints", layerNameSize);

    /* Store the layer of waypoints */

    GTMWaypointLayer* poWaypointLayer = new GTMWaypointLayer ( pszLayerName,
                                                               poSRS,
                                                               FALSE,
                                                               this );
    papoLayers[nLayers++] = poWaypointLayer;
    CPLFree(pszLayerName);

    /* Track layer */
    layerNameSize = strlen(pszBaseFileName) + sizeof("_tracks");
    pszLayerName = (char*) CPLMalloc(layerNameSize);
    /* The layer name will be "<basename>_tracks" */
    strcpy (pszLayerName, pszBaseFileName);
    CPLStrlcat (pszLayerName, "_tracks", layerNameSize);

    CPLFree(pszBaseFileName);
    /* Store the layer of tracks */
    GTMTrackLayer* poTrackLayer = new GTMTrackLayer ( pszLayerName,
                                                      poSRS,
                                                      FALSE,
                                                      this );
    papoLayers[nLayers++] = poTrackLayer;
    CPLFree(pszLayerName);

    poSRS->Release();
    return TRUE;
}

/************************************************************************/
/*                               Create()                               */
/************************************************************************/

int OGRGTMDataSource::Create( const char* pszFilename,
                              CPL_UNUSED char** papszOptions )
{
    CPLAssert( nullptr != pszFilename );

    if( fpOutput != nullptr )
    {
        CPLAssert( false );
        return FALSE;
    }

/* -------------------------------------------------------------------- */
/*     Do not override exiting file.                                    */
/* -------------------------------------------------------------------- */
    VSIStatBufL sStatBuf;

    if( VSIStatL( pszFilename, &sStatBuf ) == 0 )
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "You have to delete %s before being able to create it with the GTM driver",
                 pszFilename);
        return FALSE;
    }

/* -------------------------------------------------------------------- */
/*      Create the output file.                                         */
/* -------------------------------------------------------------------- */
    pszName = CPLStrdup( pszFilename );

    fpOutput = VSIFOpenL( pszFilename, "w" );
    if( fpOutput == nullptr )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Failed to create GTM file %s.",
                  pszFilename );
        return FALSE;
    }

    // Generate a temporary file for Trackpoints
    const char* pszTmpName = CPLGenerateTempFilename(nullptr);
    pszTmpTrackpoints = CPLStrdup( pszTmpName );
    fpTmpTrackpoints = VSIFOpenL(pszTmpName , "w" );
    if( fpTmpTrackpoints == nullptr )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Failed to create temporary file %s.",
                  pszTmpName );
        return FALSE;
    }
    // Generate a temporary file for Tracks
    pszTmpName = CPLGenerateTempFilename(nullptr);
    pszTmpTracks = CPLStrdup( pszTmpName );
    fpTmpTracks = VSIFOpenL(pszTmpName , "w" );
    if( fpTmpTracks == nullptr )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Failed to create temporary file %s.",
                  pszTmpName );
        return FALSE;
    }

/* -------------------------------------------------------------------- */
/*     Output header of GTM file.                                       */
/* -------------------------------------------------------------------- */
    char* pszBaseFileName = CPLStrdup( CPLGetBasename(pszFilename) );
    size_t sizeBuffer = 175 + strlen(pszBaseFileName);
    void* pBuffer = CPLCalloc(1, sizeBuffer);
    void* pCurrentPos = pBuffer;

    // Write version number
    appendUShort(pCurrentPos, 211);
    pCurrentPos = ((char*)pCurrentPos) + 2;
    // Write code
    strcpy((char*)pCurrentPos, "TrackMaker");
    // gradnum
    pCurrentPos = (char*) pBuffer + 14;
    appendUChar(pCurrentPos, 8);
    // bcolor
    pCurrentPos = (char*) pBuffer + 23;
    appendInt(pCurrentPos, 0xffffff);
    // nwptstyles -- We just create the defaults, so four
    pCurrentPos = (char*) pBuffer + 27;
    appendInt(pCurrentPos, 4);
    // gradfont, labelfont
    pCurrentPos = (char*) pBuffer + 99;
    for (int i = 0; i < 2; i++)
    {
        appendUShort(pCurrentPos, 5);
        pCurrentPos = ((char*)pCurrentPos) + 2;
        strcpy((char*)pCurrentPos, "Arial");
        pCurrentPos = ((char*)pCurrentPos) + 5;
    }
    appendUShort(pCurrentPos, (unsigned short) strlen(pszBaseFileName));
    pCurrentPos = ((char*)pCurrentPos) + 2;
    strcpy((char*)pCurrentPos, pszBaseFileName);

    // Write ndatum. We are implementing just WGS84, so write the
    // corresponding value for WGS84.
    pCurrentPos = ((char*) pBuffer) + 151 + strlen(pszBaseFileName);
    appendInt(pCurrentPos, 217);

    VSIFWriteL(pBuffer, sizeBuffer, 1, fpOutput);

    CPLFree(pszBaseFileName);
    CPLFree(pBuffer);
    return TRUE;
}

/************************************************************************/
/*                            GetLayer()                                */
/************************************************************************/

OGRLayer* OGRGTMDataSource::GetLayer( int iLayer )
{
    if( iLayer < 0 || iLayer >= nLayers )
        return nullptr;

    return papoLayers[iLayer];
}

/************************************************************************/
/*                           ICreateLayer()                             */
/************************************************************************/

OGRLayer * OGRGTMDataSource::ICreateLayer( const char * pszLayerName,
                                           OGRSpatialReference *poSRS,
                                           OGRwkbGeometryType eType,
                                           char ** /* papszOptions */ )
{
    if (eType == wkbPoint || eType == wkbPoint25D)
    {
        // Waypoints
        nLayers++;
        papoLayers = (OGRGTMLayer **) CPLRealloc(papoLayers, nLayers * sizeof(OGRGTMLayer*));
        papoLayers[nLayers-1] = new GTMWaypointLayer( pszName, poSRS, TRUE, this );
        return papoLayers[nLayers-1];
    }
    else if (eType == wkbLineString || eType == wkbLineString25D ||
             eType == wkbMultiLineString || eType == wkbMultiLineString25D)
    {
        // Tracks
        nLayers++;
        papoLayers = (OGRGTMLayer **) CPLRealloc(papoLayers, nLayers * sizeof(OGRGTMLayer*));
        papoLayers[nLayers-1] = new GTMTrackLayer( pszName, poSRS, TRUE, this );
        return papoLayers[nLayers-1];
    }
    else if (eType == wkbUnknown)
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "Cannot create GTM layer %s with unknown geometry type", pszLayerName);
        return nullptr;
    }
    else
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "Geometry type of `%s' not supported in GTM.\n",
                  OGRGeometryTypeToName(eType) );
        return nullptr;
    }
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRGTMDataSource::TestCapability( const char * pszCap )

{
    if( EQUAL(pszCap,ODsCCreateLayer) )
        return TRUE;
    else
        return FALSE;
}

/************************************************************************/
/*              Methods for creating a new gtm file                     */
/************************************************************************/
void OGRGTMDataSource::checkBounds(float newLat,
                                   float newLon)
{
    if (minlat == 0 && maxlat == 0 &&
        minlon == 0 && maxlon == 0)
    {
        minlat = newLat;
        maxlat = newLat;
        minlon = newLon;
        maxlon = newLon;
    }
    else
    {
        minlat = std::min(newLat, minlat);
        maxlat = std::max(newLat, maxlat);
        minlon = std::min(newLon, minlon);
        maxlon = std::max(newLon, maxlon);
    }
}

/************************************************************************/
/*               Methods for reading existent file                      */
/************************************************************************/

/*======================================================================*/
/*                           Waypoint Methods                           */
/*======================================================================*/

/*----------------------------------------------------------------------*/
/*                              getNWpts()                              */
/*----------------------------------------------------------------------*/

int OGRGTMDataSource::getNWpts()
{
    if (poGTMFile == nullptr)
        return 0;

    return poGTMFile->getNWpts();
}

/*----------------------------------------------------------------------*/
/*                         hasNextWaypoint()                            */
/*----------------------------------------------------------------------*/
bool OGRGTMDataSource::hasNextWaypoint()
{
    if (poGTMFile == nullptr)
        return false;

    return poGTMFile->hasNextWaypoint();
}

/*----------------------------------------------------------------------*/
/*                       fetchNextWaypoint()                            */
/*----------------------------------------------------------------------*/
Waypoint* OGRGTMDataSource::fetchNextWaypoint()
{
    if (poGTMFile == nullptr)
        return nullptr;

    return poGTMFile->fetchNextWaypoint();
}

/*----------------------------------------------------------------------*/
/*                         rewindWaypoint()                            */
/*----------------------------------------------------------------------*/
void OGRGTMDataSource::rewindWaypoint()
{
    if (poGTMFile == nullptr)
        return;

    poGTMFile->rewindWaypoint();
}

/*======================================================================*/
/*                             Tracks Methods                           */
/*======================================================================*/

/*----------------------------------------------------------------------*/
/*                              getNTracks()                            */
/*----------------------------------------------------------------------*/

int OGRGTMDataSource::getNTracks()
{
    if (poGTMFile == nullptr)
        return 0;

    return poGTMFile->getNTracks();
}

/*----------------------------------------------------------------------*/
/*                             hasNextTrack()                           */
/*----------------------------------------------------------------------*/
bool OGRGTMDataSource::hasNextTrack()
{
    if (poGTMFile == nullptr)
        return false;

    return poGTMFile->hasNextTrack();
}

/*----------------------------------------------------------------------*/
/*                            fetchNextTrack()                          */
/*----------------------------------------------------------------------*/
Track* OGRGTMDataSource::fetchNextTrack()
{
    if (poGTMFile == nullptr)
        return nullptr;

    return poGTMFile->fetchNextTrack();
}

/*----------------------------------------------------------------------*/
/*                           rewindTrack()                              */
/*----------------------------------------------------------------------*/
void OGRGTMDataSource::rewindTrack()
{
    if (poGTMFile == nullptr)
        return;

    poGTMFile->rewindTrack();
}
