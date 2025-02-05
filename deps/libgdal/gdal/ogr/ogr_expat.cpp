/******************************************************************************
 *
 * Project:  OGR
 * Purpose:  Convenience function for parsing with Expat library
 * Author:   Even Rouault, even dot rouault at spatialys.com
 *
 ******************************************************************************
 * Copyright (c) 2009-2012, Even Rouault <even dot rouault at spatialys.com>
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

#ifdef HAVE_EXPAT

#include "cpl_port.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "ogr_expat.h"

#include <cstddef>
#include <cstdlib>

#include "cpl_error.h"


CPL_CVSID("$Id: ogr_expat.cpp  $")

constexpr size_t OGR_EXPAT_MAX_ALLOWED_ALLOC = 10000000;

static void* OGRExpatMalloc( size_t size ) CPL_WARN_UNUSED_RESULT;
static void* OGRExpatRealloc( void *ptr, size_t size ) CPL_WARN_UNUSED_RESULT;

/************************************************************************/
/*                              CanAlloc()                              */
/************************************************************************/

static bool CanAlloc( size_t size )
{
    if( size < OGR_EXPAT_MAX_ALLOWED_ALLOC )
        return true;

    if( CPLTestBool(CPLGetConfigOption("OGR_EXPAT_UNLIMITED_MEM_ALLOC", "NO")) )
        return true;

    CPLError(CE_Failure, CPLE_OutOfMemory,
             "Expat tried to malloc %d bytes. File probably corrupted. "
             "This may also happen in case of a very big XML comment, in which case "
             "you may define the OGR_EXPAT_UNLIMITED_MEM_ALLOC configuration "
             "option to YES to remove that protection.",
             static_cast<int>(size));
    return false;
}

/************************************************************************/
/*                          OGRExpatMalloc()                            */
/************************************************************************/

static void* OGRExpatMalloc( size_t size )
{
    if( CanAlloc(size) )
        return malloc(size);

    return nullptr;
}

/************************************************************************/
/*                         OGRExpatRealloc()                            */
/************************************************************************/

// Caller must replace the pointer with the returned pointer.
static void* OGRExpatRealloc( void *ptr, size_t size )
{
    if( CanAlloc(size) )
        return realloc(ptr, size);

    return nullptr;
}

/************************************************************************/
/*                            FillWINDOWS1252()                         */
/************************************************************************/

static void FillWINDOWS1252( XML_Encoding *info )
{
    // Map CP1252 bytes to Unicode values.
    for( int i = 0; i < 0x80; ++i )
        info->map[i] = i;

    info->map[0x80] = 0x20AC;
    info->map[0x81] = -1;
    info->map[0x82] = 0x201A;
    info->map[0x83] = 0x0192;
    info->map[0x84] = 0x201E;
    info->map[0x85] = 0x2026;
    info->map[0x86] = 0x2020;
    info->map[0x87] = 0x2021;
    info->map[0x88] = 0x02C6;
    info->map[0x89] = 0x2030;
    info->map[0x8A] = 0x0160;
    info->map[0x8B] = 0x2039;
    info->map[0x8C] = 0x0152;
    info->map[0x8D] = -1;
    info->map[0x8E] = 0x017D;
    info->map[0x8F] = -1;
    info->map[0x90] = -1;
    info->map[0x91] = 0x2018;
    info->map[0x92] = 0x2019;
    info->map[0x93] = 0x201C;
    info->map[0x94] = 0x201D;
    info->map[0x95] = 0x2022;
    info->map[0x96] = 0x2013;
    info->map[0x97] = 0x2014;
    info->map[0x98] = 0x02DC;
    info->map[0x99] = 0x2122;
    info->map[0x9A] = 0x0161;
    info->map[0x9B] = 0x203A;
    info->map[0x9C] = 0x0153;
    info->map[0x9D] = -1;
    info->map[0x9E] = 0x017E;
    info->map[0x9F] = 0x0178;

    for( int i = 0xA0; i <= 0xFF; ++i )
        info->map[i] = i;
}

/************************************************************************/
/*                             FillISO885915()                          */
/************************************************************************/

static void FillISO885915( XML_Encoding *info )
{
    // Map ISO-8859-15 bytes to Unicode values.
    // Generated by generate_encoding_table.c.
    for( int i = 0x00; i < 0xA4; ++i)
        info->map[i] = i;
    info->map[0xA4] = 0x20AC;
    info->map[0xA5] = 0xA5;
    info->map[0xA6] = 0x0160;
    info->map[0xA7] = 0xA7;
    info->map[0xA8] = 0x0161;
    for( int i = 0xA9; i < 0xB4; ++i )
        info->map[i] = i;
    info->map[0xB4] = 0x017D;
    for( int i = 0xB5; i < 0xB8; ++i )
        info->map[i] = i;
    info->map[0xB8] = 0x017E;
    for( int i = 0xB9; i < 0xBC; ++i )
        info->map[i] = i;
    info->map[0xBC] = 0x0152;
    info->map[0xBD] = 0x0153;
    info->map[0xBE] = 0x0178;
    for( int i = 0xBF; i < 0x100; ++i )
        info->map[i] = i;
}

/************************************************************************/
/*                  OGRExpatUnknownEncodingHandler()                    */
/************************************************************************/

static int OGRExpatUnknownEncodingHandler(
    void * /* unused_encodingHandlerData */,
    const XML_Char *name,
    XML_Encoding *info )
{
    if( EQUAL(name, "WINDOWS-1252") )
        FillWINDOWS1252(info);
    else if( EQUAL(name, "ISO-8859-15") )
        FillISO885915(info);
    else
    {
        CPLDebug("OGR", "Unhandled encoding %s", name);
        return XML_STATUS_ERROR;
    }

    info->data    = nullptr;
    info->convert = nullptr;
    info->release = nullptr;

    return XML_STATUS_OK;
}

/************************************************************************/
/*                       OGRCreateExpatXMLParser()                      */
/************************************************************************/

XML_Parser OGRCreateExpatXMLParser()
{
    XML_Memory_Handling_Suite memsuite;
    memsuite.malloc_fcn = OGRExpatMalloc;
    memsuite.realloc_fcn = OGRExpatRealloc;
    memsuite.free_fcn = free;
    XML_Parser hParser = XML_ParserCreate_MM(nullptr, &memsuite, nullptr);

    XML_SetUnknownEncodingHandler(hParser,
                                  OGRExpatUnknownEncodingHandler,
                                  nullptr);

    return hParser;
}

#endif  // HAVE_EXPAT
