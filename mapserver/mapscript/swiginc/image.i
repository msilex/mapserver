/* ===========================================================================
   $Id$
 
   Project:  MapServer
   Purpose:  SWIG interface file for mapscript imageObj extensions
   Author:   Steve Lime 
             Sean Gillies, sgillies@frii.com
             
   ===========================================================================
   Copyright (c) 1996-2001 Regents of the University of Minnesota.
   
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
 
   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.
 
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   =========================================================================*/

//TODO : should take image type as argument ??
%extend imageObj {
   
    /* imageObj constructor now takes filename as an optional argument.
     * If the target language is Python, we ignore this constructor and
     * instead use the one in python/pymodule.i. */
#ifndef SWIGPYTHON
    imageObj(int width, int height, const char *driver=NULL,
             const char *file=NULL, mapObj *map=NULL)
    {
        imageObj *image=NULL;
        outputFormatObj *format;

        if (file) {
            return (imageObj *) msImageLoadGD(file);
        }
        else {
            if (driver) {
                format = msCreateDefaultOutputFormat(NULL, driver);
            }
            else {
                format = msCreateDefaultOutputFormat(NULL, "GD/GIF");
                if (format == NULL)
                    format = msCreateDefaultOutputFormat(NULL, "GD/PNG");
                if (format == NULL)
                    format = msCreateDefaultOutputFormat(NULL, "GD/JPEG");
                if (format == NULL)
                    format = msCreateDefaultOutputFormat(NULL, "GD/WBMP");
            }
            if (format == NULL) {
                msSetError(MS_IMGERR, "Could not create output format %s",
                           "imageObj()", driver);
                return NULL;
            }
            image = msImageCreate(width, height, format, NULL, NULL, map);
            return image;
        }
    }
#endif // SWIGPYTHON

    ~imageObj() 
    {
        msFreeImage(self);    
    }

    /* saveGeo - see Bugzilla issue 549 */ 
    void save(char *filename, mapObj *map=NULL) 
    {
        msSaveImage(map, self, filename );
    }

    /* ======================================================================
       write()

       Write image data to an open file handle.  Intended to replace
       saveToString.  See python/pyextend.i for the Python specific
       version of this method.
    ====================================================================== */
#ifndef SWIGPYTHON
    int write( FILE *file=NULL )
    {
        if ( MS_DRIVER_GD(self->format) )
            return msSaveImageStreamGD(self->img.gd, file, self->format);
        else
        {
            msSetError(MS_IMGERR, "Writing of %s format not implemented",
                       "imageObj::write");
            return MS_FAILURE;
        }
    }
#endif

    /* -----------------------------------------------------------------------
       saveToString is deprecated and no longer supported.  Users should use
       the write() method instead.
    ----------------------------------------------------------------------- */
#if defined SWIGTCL8

    Tcl_Obj *saveToString() 
    {

        unsigned char *imgbytes;
        int size;
        Tcl_Obj *imgstring;

#if GD2_VERS > 1
        if(self->format->imagemode == MS_IMAGEMODE_RGBA) {
            gdImageSaveAlpha(self->img.gd, 1);
        } else if (self->format->imagemode == MS_IMAGEMODE_RGB) {
            gdImageSaveAlpha(self->img.gd, 0);
        }
#endif 

        if(strcasecmp("ON", msGetOutputFormatOption(self->format, "INTERLACE", "ON" )) == 0) {
            gdImageInterlace(self->img.gd, 1);
        }

        if(self->format->transparent) {
            gdImageColorTransparent(self->img.gd, 0);
        }

        if(strcasecmp(self->format->driver, "gd/gif") == 0) {

#ifdef USE_GD_GIF
        imgbytes = gdImageGifPtr(self->img.gd, &size);
#else
        msSetError(MS_MISCERR, "GIF output is not available.", "saveToString()");
        return(MS_FAILURE);
#endif

        } else if (strcasecmp(self->format->driver, "gd/png") == 0) {

#ifdef USE_GD_PNG
        imgbytes = gdImagePngPtr(self->img.gd, &size);
#else
        msSetError(MS_MISCERR, "PNG output is not available.", "saveToString()");
        return(MS_FAILURE);
#endif

        } else if (strcasecmp(self->format->driver, "gd/jpeg") == 0) {

#ifdef USE_GD_JPEG
        imgbytes = gdImageJpegPtr(self->img.gd, &size, atoi(msGetOutputFormatOption(self->format, "QUALITY", "75" )));
#else
        msSetError(MS_MISCERR, "JPEG output is not available.", "saveToString()");
        return(MS_FAILURE);
#endif

        } else if (strcasecmp(self->format->driver, "gd/wbmp") == 0) {

#ifdef USE_GD_WBMP
            imgbytes = gdImageWBMPPtr(self->img.gd, &size, 1);
#else
            msSetError(MS_MISCERR, "WBMP output is not available.", "saveToString()");
            return(MS_FAILURE);
#endif
        
        } else {
            msSetError(MS_MISCERR, "Unknown output image type driver: %s.", "saveToString()", self->format->driver );
            return(MS_FAILURE);
        } 

        // Tcl implementation to create string
        imgstring = Tcl_NewByteArrayObj(imgbytes, size);    
    
        gdFree(imgbytes); // The gd docs recommend gdFree()

        return imgstring;
    }
#endif

}

