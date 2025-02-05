/******************************************************************************
 * $Id: pcidskdataset2.h  $
 *
 * Project:  PCIDSK Database File
 * Purpose:  Read/write PCIDSK Database File used by the PCI software, using
 *           the external PCIDSK library.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2009, Frank Warmerdam <warmerdam@pobox.com>
 * Copyright (c) 2009-2013, Even Rouault <even dot rouault at spatialys.com>
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

#ifndef PCIDSKDATASET2_H_INCLUDED
#define PCIDSKDATASET2_H_INCLUDED

#define GDAL_PCIDSK_DRIVER

#include "cpl_string.h"
#include "gdal_pam.h"
#include "ogrsf_frmts.h"
#include "ogr_spatialref.h"
#include "pcidsk.h"
#include "pcidsk_pct.h"
#include "pcidsk_vectorsegment.h"

#include <unordered_map>

using namespace PCIDSK;

class OGRPCIDSKLayer;

/************************************************************************/
/*                              PCIDSK2Dataset                           */
/************************************************************************/

class PCIDSK2Dataset final: public GDALPamDataset
{
    friend class PCIDSK2Band;

    mutable OGRSpatialReference* m_poSRS = nullptr;

    std::unordered_map<std::string, std::string> m_oCacheMetadataItem{};
    char      **papszLastMDListValue;

    PCIDSK::PCIDSKFile  *poFile;

    std::vector<OGRPCIDSKLayer*> apoLayers;

    static GDALDataType  PCIDSKTypeToGDAL( PCIDSK::eChanType eType );
    void                 ProcessRPC();

  public:
                PCIDSK2Dataset();
    virtual ~PCIDSK2Dataset();

    static int           Identify( GDALOpenInfo * );
    static GDALDataset  *Open( GDALOpenInfo * );
    static GDALDataset  *LLOpen( const char *pszFilename, PCIDSK::PCIDSKFile *,
                                 GDALAccess eAccess,
                                 char** papszSiblingFiles = nullptr );
    static GDALDataset  *Create( const char * pszFilename,
                                 int nXSize, int nYSize, int nBands,
                                 GDALDataType eType,
                                 char **papszParamList );

    char              **GetFileList() override;
    CPLErr              GetGeoTransform( double * padfTransform ) override;
    CPLErr              SetGeoTransform( double * ) override;

    const OGRSpatialReference* GetSpatialRef() const override;
    CPLErr SetSpatialRef(const OGRSpatialReference* poSRS) override;

    virtual char      **GetMetadataDomainList() override;
    CPLErr              SetMetadata( char **, const char * ) override;
    char              **GetMetadata( const char* ) override;
    CPLErr              SetMetadataItem(const char*,const char*,const char*) override;
    const char         *GetMetadataItem( const char*, const char*) override;

    virtual void FlushCache(bool bAtClosing) override;

    virtual CPLErr IBuildOverviews( const char *, int, int *,
                                    int, int *, GDALProgressFunc, void * ) override;

    virtual int                 GetLayerCount() override { return (int) apoLayers.size(); }
    virtual OGRLayer            *GetLayer( int ) override;

    virtual int                 TestCapability( const char * ) override;

    virtual OGRLayer           *ICreateLayer( const char *, OGRSpatialReference *,
                                     OGRwkbGeometryType, char ** ) override;
};

/************************************************************************/
/*                             PCIDSK2Band                              */
/************************************************************************/

class PCIDSK2Band final: public GDALPamRasterBand
{
    friend class PCIDSK2Dataset;

    PCIDSK::PCIDSKChannel *poChannel;
    PCIDSK::PCIDSKFile *poFile;

    void        RefreshOverviewList();
    std::vector<PCIDSK2Band*> apoOverviews;

    std::unordered_map<std::string, std::string> m_oCacheMetadataItem{};
    char      **papszLastMDListValue;

    bool        CheckForColorTable();
    GDALColorTable *poColorTable;
    bool        bCheckedForColorTable;
    int         nPCTSegNumber;

    char      **papszCategoryNames;

    void        Initialize();

  public:
                PCIDSK2Band( PCIDSK::PCIDSKFile *poFileIn,
                             PCIDSK::PCIDSKChannel *poChannelIn );
    explicit    PCIDSK2Band( PCIDSK::PCIDSKChannel * );
    virtual ~PCIDSK2Band();

    virtual CPLErr IReadBlock( int, int, void * ) override;
    virtual CPLErr IWriteBlock( int, int, void * ) override;

    virtual int        GetOverviewCount() override;
    virtual GDALRasterBand *GetOverview(int) override;

    virtual GDALColorInterp GetColorInterpretation() override;
    virtual GDALColorTable *GetColorTable() override;
    virtual CPLErr SetColorTable( GDALColorTable * ) override;

    virtual void        SetDescription( const char * ) override;

    virtual char      **GetMetadataDomainList() override;
    CPLErr              SetMetadata( char **, const char * ) override;
    char              **GetMetadata( const char* ) override;
    CPLErr              SetMetadataItem(const char*,const char*,const char*) override;
    const char         *GetMetadataItem( const char*, const char*) override;

    virtual char      **GetCategoryNames() override;
};

/************************************************************************/
/*                             OGRPCIDSKLayer                              */
/************************************************************************/

class OGRPCIDSKLayer final: public OGRLayer, public OGRGetNextFeatureThroughRaw<OGRPCIDSKLayer>
{
    PCIDSK::PCIDSKVectorSegment *poVecSeg;
    PCIDSK::PCIDSKSegment       *poSeg;

    OGRFeatureDefn     *poFeatureDefn;

    OGRFeature *        GetNextRawFeature();

    int                 iRingStartField;
    PCIDSK::ShapeId     hLastShapeId;

    bool                bUpdateAccess;

    OGRSpatialReference *poSRS;

    std::unordered_map<std::string, int> m_oMapFieldNameToIdx{};
    bool                m_bEOF = false;

  public:
    OGRPCIDSKLayer( PCIDSK::PCIDSKSegment*, PCIDSK::PCIDSKVectorSegment *, bool bUpdate );
    virtual ~OGRPCIDSKLayer();

    void                ResetReading() override;
    DEFINE_GET_NEXT_FEATURE_THROUGH_RAW(OGRPCIDSKLayer)

    OGRFeature         *GetFeature( GIntBig nFeatureId ) override;
    virtual OGRErr      ISetFeature( OGRFeature *poFeature ) override;

    OGRFeatureDefn *    GetLayerDefn() override { return poFeatureDefn; }

    int                 TestCapability( const char * ) override;

    OGRErr              DeleteFeature( GIntBig nFID ) override;
    virtual OGRErr      ICreateFeature( OGRFeature *poFeature ) override;
    virtual OGRErr      CreateField( OGRFieldDefn *poField,
                                     int bApproxOK = TRUE ) override;

    GIntBig             GetFeatureCount( int ) override;
    OGRErr              GetExtent( OGREnvelope *psExtent, int bForce ) override;
    virtual OGRErr      GetExtent(int iGeomField, OGREnvelope *psExtent, int bForce) override
                { return OGRLayer::GetExtent(iGeomField, psExtent, bForce); }
};

#endif /*  PCIDSKDATASET2_H_INCLUDED */
