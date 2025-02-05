/******************************************************************************
 *
 * Project:  OGR/DODS Interface
 * Purpose:  Implements OGRDODSFieldDefn class.  This is a small class used
 *           to encapsulate information about a referenced field.
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

CPL_CVSID("$Id: ogrdodsfielddefn.cpp  $")

/************************************************************************/
/*                          OGRDODSFieldDefn()                          */
/************************************************************************/

OGRDODSFieldDefn::OGRDODSFieldDefn() :
    bValid(false),
    pszFieldName(nullptr),
    pszFieldScope(nullptr),
    iFieldIndex(-1),
    pszFieldValue(nullptr),
    pszPathToSequence(nullptr),
    bRelativeToSuperSequence(false),
    bRelativeToSequence(false)
{}

/************************************************************************/
/*                         ~OGRDODSFieldDefn()                          */
/************************************************************************/

OGRDODSFieldDefn::~OGRDODSFieldDefn()

{
    CPLFree( pszFieldName );
    CPLFree( pszFieldScope );
    CPLFree( pszFieldValue );
    CPLFree( pszPathToSequence );
}

/************************************************************************/
/*                             Initialize()                             */
/*                                                                      */
/*      Build field reference from a DAS entry.  The AttrTable          */
/*      passed should be the container of the field defn.  For          */
/*      instance, the "x_field" node with a name and scope sub          */
/*      entry.                                                          */
/************************************************************************/

bool OGRDODSFieldDefn::Initialize( AttrTable *poEntry,
                                   BaseType *poTarget,
                                   BaseType *poSuperSeq )

{
    auto osScope(poEntry->get_attr("scope"));
    const char *l_pszFieldScope = osScope.c_str();
    if( osScope.empty() )
        l_pszFieldScope = "dds";

    return Initialize( poEntry->get_attr("name").c_str(), l_pszFieldScope,
                       poTarget, poSuperSeq );
}

/************************************************************************/
/*                             Initialize()                             */
/************************************************************************/

bool OGRDODSFieldDefn::Initialize( const char *pszFieldNameIn,
                                   const char *pszFieldScopeIn,
                                   BaseType *poTarget,
                                   BaseType *poSuperSeq )

{
    pszFieldScope = CPLStrdup( pszFieldScopeIn );
    pszFieldName = CPLStrdup( pszFieldNameIn );

    if( poTarget != nullptr && EQUAL(pszFieldScope,"dds") )
    {
        string oTargPath = OGRDODSGetVarPath( poTarget );
        int    nTargPathLen = static_cast<int>(strlen(oTargPath.c_str()));

        if( EQUALN(oTargPath.c_str(),pszFieldNameIn,nTargPathLen)
            && pszFieldNameIn[nTargPathLen] == '.' )
        {
            CPLFree( pszFieldName );
            pszFieldName = CPLStrdup( pszFieldNameIn + nTargPathLen + 1 );

            bRelativeToSequence = true;
            iFieldIndex = OGRDODSGetVarIndex(
                dynamic_cast<Sequence *>( poTarget ), pszFieldName );
        }
        else if( poSuperSeq != nullptr  )
        {
            oTargPath = OGRDODSGetVarPath( poSuperSeq );
            nTargPathLen = static_cast<int>(strlen(oTargPath.c_str()));

            if( EQUALN(oTargPath.c_str(),pszFieldNameIn,nTargPathLen)
                && pszFieldNameIn[nTargPathLen] == '.' )
            {
                CPLFree( pszFieldName );
                pszFieldName = CPLStrdup( pszFieldNameIn + nTargPathLen + 1 );

                bRelativeToSuperSequence = true;
                iFieldIndex = OGRDODSGetVarIndex(
                    dynamic_cast<Sequence *>( poSuperSeq ), pszFieldName );
            }
        }
    }

    bValid = true;

    return true;
}

/************************************************************************/
/*                         OGRDODSGetVarPath()                          */
/*                                                                      */
/*      Return the full path to a variable.                             */
/************************************************************************/

string OGRDODSGetVarPath( BaseType *poTarget )

{
    string oFullName;

    oFullName = poTarget->name();

    while( (poTarget = poTarget->get_parent()) != nullptr )
    {
        oFullName = poTarget->name() + "." + oFullName;
    }

    return oFullName;
}

/************************************************************************/
/*                         OGRDODSGetVarIndex()                         */
/************************************************************************/

int  OGRDODSGetVarIndex( Sequence *poParent, string oVarName )

{
    Sequence::Vars_iter v_i;
    int                 i;

    for( v_i = poParent->var_begin(), i=0;
         v_i != poParent->var_end();
         v_i++, i++ )
    {
        if( EQUAL((*v_i)->name().c_str(),oVarName.c_str()) )
            return i;
    }

    return -1;
}
