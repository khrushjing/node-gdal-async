/******************************************************************************
 * $Id: kmlvector.h  $
 *
 * Project:  KML Driver
 * Purpose:  Specialization of the kml class, only for vectors in kml files.
 * Author:   Jens Oberender, j.obi@troja.net
 *
 ******************************************************************************
 * Copyright (c) 2007, Jens Oberender
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
#ifndef OGR_KMLVECTOR_H_INCLUDED
#define OGR_KMLVECTOR_H_INCLUDED

#ifdef HAVE_EXPAT

#include "kml.h"
#include "kmlnode.h"
// std
#include <string>

class KMLVector : public KML
{
public:
    ~KMLVector();

    // Container - FeatureContainer - Feature
    bool isFeature(std::string const& sIn) const override;
    bool isFeatureContainer(std::string const& sIn) const override;
    bool isContainer(std::string const& sIn) const override;
    bool isLeaf(std::string const& sIn) const override;
    bool isRest(std::string const& sIn) const override;
    void findLayers(KMLNode* poNode, int bKeepEmptyContainers) override;
};

#endif // HAVE_EXPAT

#endif /* OGR_KMLVECTOR_H_INCLUDED */

