/******************************************************************************
 *
 * Project:  GDAL algorithms
 * Purpose:  Delaunay triangulation
 * Author:   Even Rouault, even.rouault at spatialys.com
 *
 ******************************************************************************
 * Copyright (c) 2015, Even Rouault <even.rouault at spatialys.com>
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

#if defined(__MINGW32__) || defined(__MINGW64__)
/*  This avoids i586-mingw32msvc/include/direct.h from including libqhull/io.h ... */
#define _DIRECT_H_
/* For __MINGW64__ */
#define _INC_DIRECT
#define _INC_STAT
#endif

#if defined(INTERNAL_QHULL) && !defined(DONT_DEPRECATE_SPRINTF)
#define DONT_DEPRECATE_SPRINTF
#endif

#include "cpl_error.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "gdal_alg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

CPL_CVSID("$Id: delaunay.c  $")

#if defined(INTERNAL_QHULL) || defined(EXTERNAL_QHULL)
#define HAVE_INTERNAL_OR_EXTERNAL_QHULL 1
#endif

#if HAVE_INTERNAL_OR_EXTERNAL_QHULL
#ifdef INTERNAL_QHULL

#include "internal_qhull_headers.h"

#else /* INTERNAL_QHULL */

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4324 ) // 'qhT': structure was padded due to alignment specifier
#endif

#include "libqhull_r/libqhull_r.h"
#include "libqhull_r/qset_r.h"

#ifdef _MSC_VER
#pragma warning( pop )
#endif

#endif /* INTERNAL_QHULL */

#endif /* HAVE_INTERNAL_OR_EXTERNAL_QHULL*/


/************************************************************************/
/*                       GDALHasTriangulation()                         */
/************************************************************************/

/** Returns if GDAL is built with Delaunay triangulation support.
 *
 * @return TRUE if GDAL is built with Delaunay triangulation support.
 *
 * @since GDAL 2.1
 */
int GDALHasTriangulation()
{
#if HAVE_INTERNAL_OR_EXTERNAL_QHULL
    return TRUE;
#else
    return FALSE;
#endif
}

/************************************************************************/
/*                   GDALTriangulationCreateDelaunay()                  */
/************************************************************************/

/** Computes a Delaunay triangulation of the passed points
 *
 * @param nPoints number of points
 * @param padfX x coordinates of the points.
 * @param padfY y coordinates of the points.
 * @return triangulation that must be freed with GDALTriangulationFree(), or
 *         NULL in case of error.
 *
 * @since GDAL 2.1
 */
GDALTriangulation* GDALTriangulationCreateDelaunay(int nPoints,
                                                   const double* padfX,
                                                   const double* padfY)
{
#if HAVE_INTERNAL_OR_EXTERNAL_QHULL
    coordT* points;
    int i, j;
    GDALTriangulation* psDT = NULL;
    facetT *facet;
    GDALTriFacet* pasFacets;
    int* panMapQHFacetIdToFacetIdx; /* map from QHull facet ID to the index of our GDALTriFacet* array */
    int curlong, totlong;     /* memory remaining after qh_memfreeshort */
    char* pszTempFilename = NULL;
    FILE* fpTemp = NULL;

    qhT qh_qh;
    qhT *qh= &qh_qh;

    QHULL_LIB_CHECK /* Check for compatible library */

    points = (coordT*)VSI_MALLOC2_VERBOSE(sizeof(double)*2, nPoints);
    if( points == NULL )
    {
        return NULL;
    }
    for(i=0;i<nPoints;i++)
    {
        points[2*i] = padfX[i];
        points[2*i+1] = padfY[i];
    }

    qh_meminit(qh, NULL);

    if( CPLTestBoolean(CPLGetConfigOption("QHULL_LOG_TO_TEMP_FILE", "NO")) )
    {
        pszTempFilename = CPLStrdup(CPLGenerateTempFilename(NULL));
        fpTemp = fopen(pszTempFilename, "wb");
    }
    if( fpTemp == NULL )
        fpTemp = stderr;

    /* d: Delaunay */
    /* Qbb: scale last coordinate to [0,m] for Delaunay */
    /* Qc: keep coplanar points with nearest facet */
    /* Qz: add a point-at-infinity for Delaunay triangulation */
    /* Qt: triangulated output */
    int ret = qh_new_qhull(qh,
                     2, nPoints, points, FALSE /* ismalloc */,
                     "qhull d Qbb Qc Qz Qt", NULL, fpTemp);
    if( fpTemp != stderr )
    {
        fclose(fpTemp);
    }
    if( pszTempFilename != NULL )
    {
        VSIUnlink(pszTempFilename);
        VSIFree(pszTempFilename);
    }

    VSIFree(points);
    points = NULL;

    if( ret != 0 )
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Delaunay triangulation failed");
        goto end;
    }

    /* Establish a map from QHull facet id to the index in our array of sequential facets */
    panMapQHFacetIdToFacetIdx = (int*)VSI_MALLOC2_VERBOSE(sizeof(int), qh->facet_id);
    if( panMapQHFacetIdToFacetIdx == NULL )
    {
        goto end;
    }
    memset(panMapQHFacetIdToFacetIdx, 0xFF, sizeof(int) * qh->facet_id);

    for(j = 0, facet = qh->facet_list;
        facet != NULL && facet->next != NULL;
        facet = facet->next)
    {
        if( facet->upperdelaunay != qh->UPPERdelaunay )
            continue;

        if( qh_setsize(qh, facet->vertices) != 3 ||
            qh_setsize(qh, facet->neighbors) != 3 )
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "Triangulation resulted in non triangular facet %d: vertices=%d",
                     facet->id, qh_setsize(qh, facet->vertices));
            VSIFree(panMapQHFacetIdToFacetIdx);
            goto end;
        }

        CPLAssert(facet->id < qh->facet_id);
        panMapQHFacetIdToFacetIdx[facet->id] = j++;
    }

    pasFacets = (GDALTriFacet*) VSI_MALLOC2_VERBOSE( j, sizeof(GDALTriFacet) );
    if(pasFacets == NULL )
    {
        VSIFree(panMapQHFacetIdToFacetIdx);
        goto end;
    }

    psDT = (GDALTriangulation*)CPLCalloc(1, sizeof(GDALTriangulation));
    psDT->nFacets = j;
    psDT->pasFacets = pasFacets;

    // Store vertex and neighbor information for each triangle.
    for(facet = qh->facet_list;
        facet != NULL && facet->next != NULL;
        facet = facet->next)
    {
        int k;
        if( facet->upperdelaunay != qh->UPPERdelaunay )
            continue;
        k = panMapQHFacetIdToFacetIdx[facet->id];
        pasFacets[k].anVertexIdx[0] =
            qh_pointid(qh, ((vertexT*) facet->vertices->e[0].p)->point);
        pasFacets[k].anVertexIdx[1] =
            qh_pointid(qh, ((vertexT*) facet->vertices->e[1].p)->point);
        pasFacets[k].anVertexIdx[2] =
            qh_pointid(qh, ((vertexT*) facet->vertices->e[2].p)->point);
        pasFacets[k].anNeighborIdx[0] =
            panMapQHFacetIdToFacetIdx[((facetT*) facet->neighbors->e[0].p)->id];
        pasFacets[k].anNeighborIdx[1] =
            panMapQHFacetIdToFacetIdx[((facetT*) facet->neighbors->e[1].p)->id];
        pasFacets[k].anNeighborIdx[2] =
            panMapQHFacetIdToFacetIdx[((facetT*) facet->neighbors->e[2].p)->id];
    }

    VSIFree(panMapQHFacetIdToFacetIdx);

end:
    qh_freeqhull(qh, !qh_ALL);
    qh_memfreeshort(qh, &curlong, &totlong);

    return psDT;
#else /* HAVE_INTERNAL_OR_EXTERNAL_QHULL */

    /* Suppress unused argument warnings. */
    (void)nPoints;
    (void)padfX;
    (void)padfY;

    CPLError(CE_Failure, CPLE_NotSupported,
             "GDALTriangulationCreateDelaunay() unavailable since GDAL built without QHull support");
    return NULL;
#endif /* HAVE_INTERNAL_OR_EXTERNAL_QHULL */
}

/************************************************************************/
/*                       GDALTriangulationFree()                        */
/************************************************************************/

/** Free a triangulation.
 *
 * @param psDT triangulation.
 * @since GDAL 2.1
 */
void GDALTriangulationFree(GDALTriangulation* psDT)
{
    if( psDT )
    {
        VSIFree(psDT->pasFacets);
        VSIFree(psDT->pasFacetCoefficients);
        VSIFree(psDT);
    }
}

/************************************************************************/
/*               GDALTriangulationComputeBarycentricCoefficients()      */
/************************************************************************/

/** Computes barycentric coefficients for each triangles of the triangulation.
 *
 * @param psDT triangulation.
 * @param padfX x coordinates of the points. Must be identical to the one passed
 *              to GDALTriangulationCreateDelaunay().
 * @param padfY y coordinates of the points. Must be identical to the one passed
 *              to GDALTriangulationCreateDelaunay().
 *
 * @return TRUE in case of success.
 *
 * @since GDAL 2.1
 */
int  GDALTriangulationComputeBarycentricCoefficients(GDALTriangulation* psDT,
                                                     const double* padfX,
                                                     const double* padfY)
{
    int i;

    if( psDT->pasFacetCoefficients != NULL )
    {
        return TRUE;
    }
    psDT->pasFacetCoefficients = (GDALTriBarycentricCoefficients*)VSI_MALLOC2_VERBOSE(
        sizeof(GDALTriBarycentricCoefficients), psDT->nFacets);
    if( psDT->pasFacetCoefficients == NULL )
    {
        return FALSE;
    }

    for(i = 0; i < psDT->nFacets; i++)
    {
        GDALTriFacet* psFacet = &(psDT->pasFacets[i]);
        GDALTriBarycentricCoefficients* psCoeffs = &(psDT->pasFacetCoefficients[i]);
        double dfX1 = padfX[psFacet->anVertexIdx[0]];
        double dfY1 = padfY[psFacet->anVertexIdx[0]];
        double dfX2 = padfX[psFacet->anVertexIdx[1]];
        double dfY2 = padfY[psFacet->anVertexIdx[1]];
        double dfX3 = padfX[psFacet->anVertexIdx[2]];
        double dfY3 = padfY[psFacet->anVertexIdx[2]];
        /* See https://en.wikipedia.org/wiki/Barycentric_coordinate_system */
        double dfDenom = (dfY2 - dfY3) * (dfX1 - dfX3) + (dfX3 - dfX2) * (dfY1 - dfY3);
        if( fabs(dfDenom) < 1e-5 )
        {
            // Degenerate triangle
            psCoeffs->dfMul1X = 0.0;
            psCoeffs->dfMul1Y = 0.0;
            psCoeffs->dfMul2X = 0.0;
            psCoeffs->dfMul2Y = 0.0;
            psCoeffs->dfCstX = 0.0;
            psCoeffs->dfCstY = 0.0;
        }
        else
        {
            psCoeffs->dfMul1X = (dfY2  - dfY3) / dfDenom;
            psCoeffs->dfMul1Y = (dfX3  - dfX2) / dfDenom;
            psCoeffs->dfMul2X = (dfY3  - dfY1) / dfDenom;
            psCoeffs->dfMul2Y = (dfX1  - dfX3) / dfDenom;
            psCoeffs->dfCstX = dfX3;
            psCoeffs->dfCstY = dfY3;
        }
    }
    return TRUE;
}

/************************************************************************/
/*               GDALTriangulationComputeBarycentricCoordinates()       */
/************************************************************************/

#define BARYC_COORD_L1(psCoeffs, dfX, dfY) \
        (psCoeffs->dfMul1X * ((dfX) - psCoeffs->dfCstX) + psCoeffs->dfMul1Y * ((dfY) - psCoeffs->dfCstY))
#define BARYC_COORD_L2(psCoeffs, dfX, dfY) \
        (psCoeffs->dfMul2X * ((dfX) - psCoeffs->dfCstX) + psCoeffs->dfMul2Y * ((dfY) - psCoeffs->dfCstY))
#define BARYC_COORD_L3(l1, l2)  (1 - (l1) - (l2))

/** Computes the barycentric coordinates of a point.
 *
 * @param psDT triangulation.
 * @param nFacetIdx index of the triangle in the triangulation
 * @param dfX x coordinate of the point.
 * @param dfY y coordinate of the point.
 * @param pdfL1 (output) pointer to the 1st barycentric coordinate.
 * @param pdfL2 (output) pointer to the 2nd barycentric coordinate.
 * @param pdfL3 (output) pointer to the 2nd barycentric coordinate.
 *
 * @return TRUE in case of success.
 *
 * @since GDAL 2.1
 */

int  GDALTriangulationComputeBarycentricCoordinates(const GDALTriangulation* psDT,
                                                    int nFacetIdx,
                                                    double dfX,
                                                    double dfY,
                                                    double* pdfL1,
                                                    double* pdfL2,
                                                    double* pdfL3)
{
    const GDALTriBarycentricCoefficients* psCoeffs;
    if( psDT->pasFacetCoefficients == NULL )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "GDALTriangulationComputeBarycentricCoefficients() should be called before");
        return FALSE;
    }
    CPLAssert(nFacetIdx >= 0 && nFacetIdx < psDT->nFacets);
    psCoeffs = &(psDT->pasFacetCoefficients[nFacetIdx]);
    *pdfL1 = BARYC_COORD_L1(psCoeffs, dfX, dfY);
    *pdfL2 = BARYC_COORD_L2(psCoeffs, dfX, dfY);
    *pdfL3 = BARYC_COORD_L3(*pdfL1, *pdfL2);

    return TRUE;
}

/************************************************************************/
/*               GDALTriangulationFindFacetBruteForce()                 */
/************************************************************************/

#define EPS     1e-10

/** Returns the index of the triangle that contains the point by iterating
 * over all triangles.
 *
 * If the function returns FALSE and *panOutputFacetIdx >= 0, then it means
 * the point is outside the hull of the triangulation, and *panOutputFacetIdx
 * is the closest triangle to the point.
 *
 * @param psDT triangulation.
 * @param dfX x coordinate of the point.
 * @param dfY y coordinate of the point.
 * @param panOutputFacetIdx (output) pointer to the index of the triangle,
 *                          or -1 in case of failure.
 *
 * @return index >= 0 of the triangle in case of success, -1 otherwise.
 *
 * @since GDAL 2.1
 */

int GDALTriangulationFindFacetBruteForce(const GDALTriangulation* psDT,
                                         double dfX,
                                         double dfY,
                                         int* panOutputFacetIdx)
{
    int nFacetIdx;
    *panOutputFacetIdx = -1;
    if( psDT->pasFacetCoefficients == NULL )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "GDALTriangulationComputeBarycentricCoefficients() should be called before");
        return FALSE;
    }
    for(nFacetIdx=0;nFacetIdx<psDT->nFacets;nFacetIdx++)
    {
        double l1, l2, l3;
        const GDALTriBarycentricCoefficients* psCoeffs =
                                    &(psDT->pasFacetCoefficients[nFacetIdx]);
        if( psCoeffs->dfMul1X == 0.0 && psCoeffs->dfMul2X == 0.0 &&
            psCoeffs->dfMul1Y == 0.0 && psCoeffs->dfMul2Y == 0.0 )
        {
            // Degenerate triangle
            continue;
        }
        l1 = BARYC_COORD_L1(psCoeffs, dfX, dfY);
        if( l1 < -EPS )
        {
            int neighbor = psDT->pasFacets[nFacetIdx].anNeighborIdx[0];
            if( neighbor < 0 )
            {
                *panOutputFacetIdx = nFacetIdx;
                return FALSE;
            }
            continue;
        }
        if( l1 > 1 + EPS )
            continue;
        l2 = BARYC_COORD_L2(psCoeffs, dfX, dfY);
        if( l2 < -EPS )
        {
            int neighbor = psDT->pasFacets[nFacetIdx].anNeighborIdx[1];
            if( neighbor < 0 )
            {
                *panOutputFacetIdx = nFacetIdx;
                return FALSE;
            }
            continue;
        }
        if( l2 > 1 + EPS )
            continue;
        l3 = BARYC_COORD_L3(l1, l2);
        if( l3 < -EPS )
        {
            int neighbor = psDT->pasFacets[nFacetIdx].anNeighborIdx[2];
            if( neighbor < 0 )
            {
                *panOutputFacetIdx = nFacetIdx;
                return FALSE;
            }
            continue;
        }
        if( l3 > 1 + EPS )
            continue;
        *panOutputFacetIdx = nFacetIdx;
        return TRUE;
    }
    return FALSE;
}

/************************************************************************/
/*               GDALTriangulationFindFacetDirected()                   */
/************************************************************************/

#define EPS     1e-10

/** Returns the index of the triangle that contains the point by walking in
 * the triangulation.
 *
 * If the function returns FALSE and *panOutputFacetIdx >= 0, then it means
 * the point is outside the hull of the triangulation, and *panOutputFacetIdx
 * is the closest triangle to the point.
 *
 * @param psDT triangulation.
 * @param nFacetIdx index of first triangle to start with.
 *                  Must be >= 0 && < psDT->nFacets
 * @param dfX x coordinate of the point.
 * @param dfY y coordinate of the point.
 * @param panOutputFacetIdx (output) pointer to the index of the triangle,
 *                          or -1 in case of failure.
 *
 * @return TRUE in case of success, FALSE otherwise.
 *
 * @since GDAL 2.1
 */

int GDALTriangulationFindFacetDirected(const GDALTriangulation* psDT,
                                       int nFacetIdx,
                                       double dfX,
                                       double dfY,
                                       int* panOutputFacetIdx)
{
#ifdef DEBUG_VERBOSE
    const int nFacetIdxInitial = nFacetIdx;
#endif
    int k, nIterMax;
    *panOutputFacetIdx = -1;
    if( psDT->pasFacetCoefficients == NULL )
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "GDALTriangulationComputeBarycentricCoefficients() should be called before");
        return FALSE;
    }
    CPLAssert(nFacetIdx >= 0 && nFacetIdx < psDT->nFacets);

    nIterMax = 2 + psDT->nFacets / 4;
    for(k=0;k<nIterMax;k++)
    {
        double l1, l2, l3;
        int bMatch = TRUE;
        const GDALTriFacet* psFacet = &(psDT->pasFacets[nFacetIdx]);
        const GDALTriBarycentricCoefficients* psCoeffs =
                                &(psDT->pasFacetCoefficients[nFacetIdx]);
        if( psCoeffs->dfMul1X == 0.0 && psCoeffs->dfMul2X == 0.0 &&
            psCoeffs->dfMul1Y == 0.0 && psCoeffs->dfMul2Y == 0.0 )
        {
            // Degenerate triangle
            break;
        }
        l1 = BARYC_COORD_L1(psCoeffs, dfX, dfY);
        if( l1 < -EPS )
        {
            int neighbor = psFacet->anNeighborIdx[0];
            if( neighbor < 0 )
            {
#ifdef DEBUG_VERBOSE
                CPLDebug("GDAL", "Outside %d in %d iters (initial = %d)",
                         nFacetIdx, k, nFacetIdxInitial);
#endif
                *panOutputFacetIdx = nFacetIdx;
                return FALSE;
            }
            nFacetIdx = neighbor;
            continue;
        }
        else if( l1 > 1 + EPS )
            bMatch = FALSE; // outside or degenerate

        l2 = BARYC_COORD_L2(psCoeffs, dfX, dfY);
        if( l2 < -EPS )
        {
            int neighbor = psFacet->anNeighborIdx[1];
            if( neighbor < 0 )
            {
#ifdef DEBUG_VERBOSE
                CPLDebug("GDAL", "Outside %d in %d iters (initial = %d)",
                         nFacetIdx, k, nFacetIdxInitial);
#endif
                *panOutputFacetIdx = nFacetIdx;
                return FALSE;
            }
            nFacetIdx = neighbor;
            continue;
        }
        else if( l2 > 1 + EPS )
            bMatch = FALSE; // outside or degenerate

        l3 = BARYC_COORD_L3(l1, l2);
        if( l3 < -EPS )
        {
            int neighbor = psFacet->anNeighborIdx[2];
            if( neighbor < 0 )
            {
#ifdef DEBUG_VERBOSE
                CPLDebug("GDAL", "Outside %d in %d iters (initial = %d)",
                         nFacetIdx, k, nFacetIdxInitial);
#endif
                *panOutputFacetIdx = nFacetIdx;
                return FALSE;
            }
            nFacetIdx = neighbor;
            continue;
        }
        else if( l3 > 1 + EPS )
            bMatch = FALSE; // outside or degenerate

        if( bMatch )
        {
#ifdef DEBUG_VERBOSE
            CPLDebug("GDAL", "Inside %d in %d iters (initial = %d)",
                     nFacetIdx, k, nFacetIdxInitial);
#endif
            *panOutputFacetIdx = nFacetIdx;
            return TRUE;
        }
        else
        {
            break;
        }
    }

    CPLDebug("GDAL", "Using brute force lookup");
    return GDALTriangulationFindFacetBruteForce(psDT, dfX, dfY, panOutputFacetIdx);
}
