/******************************************************************************
 * $Id$
 *
 * Project:  MapServer
 * Purpose:  Functions for operating on a classObj that don't belong in a
 *           more specific file such as mapfile.c.  
 *           Adapted from mapobject.c.
 * Author:   Sean Gillies, sgillies@frii.com
 *
 ******************************************************************************
 * Copyright (c) 2004, Sean Gillies
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

#include "map.h"

#ifdef USE_GDAL
#  include "gdal.h"
#  include "cpl_conv.h"
#endif

/**
 * Move the style up inside the array of styles.
 */  
int msMoveStyleUp(classObj *class, int nStyleIndex)
{
    styleObj *psTmpStyle = NULL;
    if (class && nStyleIndex < class->numstyles && nStyleIndex >0)
    {
        psTmpStyle = (styleObj *)malloc(sizeof(styleObj));
        initStyle(psTmpStyle);
        
        msCopyStyle(psTmpStyle, &class->styles[nStyleIndex]);

        msCopyStyle(&class->styles[nStyleIndex], 
                    &class->styles[nStyleIndex-1]);
        
        msCopyStyle(&class->styles[nStyleIndex-1], psTmpStyle);

        return(MS_SUCCESS);
    }
    msSetError(MS_CHILDERR, "Invalid index: %d", "msMoveStyleUp()",
               nStyleIndex);
    return (MS_FAILURE);
}


/**
 * Move the style down inside the array of styles.
 */  
int msMoveStyleDown(classObj *class, int nStyleIndex)
{
    styleObj *psTmpStyle = NULL;

    if (class && nStyleIndex < class->numstyles-1 && nStyleIndex >=0)
    {
        psTmpStyle = (styleObj *)malloc(sizeof(styleObj));
        initStyle(psTmpStyle);
        
        msCopyStyle(psTmpStyle, &class->styles[nStyleIndex]);

        msCopyStyle(&class->styles[nStyleIndex], 
                    &class->styles[nStyleIndex+1]);
        
        msCopyStyle(&class->styles[nStyleIndex+1], psTmpStyle);

        return(MS_SUCCESS);
    }
    msSetError(MS_CHILDERR, "Invalid index: %d", "msMoveStyleDown()",
               nStyleIndex);
    return (MS_FAILURE);
}

/* Moved here from mapscript.i 
 *
 * Returns the index at which the style was inserted
 *
 */
int msInsertStyle(classObj *class, styleObj *style, int nStyleIndex) {
    int i;

    if (!style)
    {
        msSetError(MS_CHILDERR, "Can't insert a NULL Style", "msInsertStyle()");
        return -1;
    }

    // Possible to add another style?
    if (class->numstyles == MS_MAXSTYLES) {
        msSetError(MS_CHILDERR, "Maximum number of class styles, %d, has been reached", "insertStyle()", MS_MAXSTYLES);
        return -1;
    }
    // Catch attempt to insert past end of styles array
    else if (nStyleIndex >= MS_MAXSTYLES) {
        msSetError(MS_CHILDERR, "Cannot insert style beyond index %d", "insertStyle()", MS_MAXSTYLES-1);
        return -1;
    }
    else if (nStyleIndex < 0) { // Insert at the end by default
        msCopyStyle(&(class->styles[class->numstyles]), style);
        class->numstyles++;
        return class->numstyles-1;
    }
    else if (nStyleIndex >= 0 && nStyleIndex < MS_MAXSTYLES) {
        // Move styles existing at the specified nStyleIndex or greater
        // to a higher nStyleIndex
        for (i=class->numstyles-1; i>=nStyleIndex; i--) {
            class->styles[i+1] = class->styles[i];
        }
        msCopyStyle(&(class->styles[nStyleIndex]), style);
        class->numstyles++;
        return nStyleIndex;
    }
    else {
        msSetError(MS_CHILDERR, "Invalid nStyleIndex", "insertStyle()");
        return -1;
    }
}

styleObj *msRemoveStyle(classObj *class, int nStyleIndex) {
    int i;
    styleObj *style;
    if (class->numstyles == 1) {
        msSetError(MS_CHILDERR, "Cannot remove a class's sole style", "removeStyle()");
        return NULL;
    }
    else if (nStyleIndex < 0 || nStyleIndex >= class->numstyles) {
        msSetError(MS_CHILDERR, "Cannot remove style, invalid nStyleIndex %d", "removeStyle()", nStyleIndex);
        return NULL;
    }
    else {
        style = (styleObj *)malloc(sizeof(styleObj));
        if (!style) {
            msSetError(MS_MEMERR, "Failed to allocate styleObj to return as removed style", "msRemoveStyle");
            return NULL;
        }
        msCopyStyle(style, &(class->styles[nStyleIndex]));
        style->isachild = MS_FALSE;
        for (i=nStyleIndex; i<class->numstyles-1; i++) {
             msCopyStyle(&class->styles[i], &class->styles[i+1]);
        }
        class->numstyles--;
        return style;
    }
}

/**
 * Delete the style identified by the index and shift
 * styles that follows the deleted style.
 */  
int msDeleteStyle(classObj *class, int nStyleIndex)
{
    int i = 0;
    if (class && nStyleIndex < class->numstyles && nStyleIndex >=0)
    {
        for (i=nStyleIndex; i< class->numstyles-1; i++)
        {
             msCopyStyle(&class->styles[i], &class->styles[i+1]);
        }
        class->numstyles--;
        return(MS_SUCCESS);
    }
    msSetError(MS_CHILDERR, "Invalid index: %d", "msDeleteStyle()",
               nStyleIndex);
    return (MS_FAILURE);
}
