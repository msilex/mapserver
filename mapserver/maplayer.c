/******************************************************************************
 *
 * Project:  MapServer
 * Purpose:  Implementation of most layerObj functions.
 * Author:   Steve Lime and the MapServer team.
 *
 ******************************************************************************
 * Copyright (c) 1996-2005 Regents of the University of Minnesota.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies of this Software or works derived from this Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************
 *
 * $Log$
 * Revision 1.106  2005/07/16 19:07:38  jerryp
 * Bug 1420: PostGIS connector no longer needs two layer close functions.
 *
 * Revision 1.105  2005/06/14 16:03:33  dan
 * Updated copyright date to 2005
 *
 * Revision 1.104  2005/04/25 06:41:56  sdlime
 * Applied Bill's newest gradient patch, more concise in the mapfile and potential to use via MapScript.
 *
 * Revision 1.103  2005/04/15 17:10:36  sdlime
 * Applied Bill Benko's patch for bug 1305, gradient support.
 *
 * Revision 1.102  2005/02/18 03:06:46  dan
 * Turned all C++ (//) comments into C comments (bug 1238)
 *
 * Revision 1.101  2005/01/12 21:11:21  frank
 * removed LABELS_ROTATE_WITH_MAP, rotate labels if angle!=0 or labelangleitem
 *
 * Revision 1.100  2005/01/11 00:24:59  frank
 * added msLayerLabelsRotateWithMap
 *
 * Revision 1.99  2004/11/16 19:18:43  assefa
 * Make sure that the timestring is complete for pg layers (Bug 837)
 *
 * Revision 1.98  2004/11/15 21:11:23  dan
 * Moved the layer->getExtent() logic down to msLayerGetExtent() (bug 1051)
 *
 * Revision 1.97  2004/11/15 20:35:02  dan
 * Added msLayerIsOpen() to all vector layer types (bug 1051)
 *
 * Revision 1.96  2004/10/21 10:54:17  assefa
 * Add postgis date_trunc support.
 *
 * Revision 1.95  2004/10/21 04:30:55  frank
 * Added standardized headers.  Added MS_CVSID().
 *
 */

#include "map.h"
#include "maptime.h"

MS_CVSID("$Id$")

/*
** Iteminfo is a layer parameter that holds information necessary to retrieve an individual item for
** a particular source. It is an array built from a list of items. The type of iteminfo will vary by
** source. For shapefiles and OGR it is simply an array of integers where each value is an index for
** the item. For SDE it's a ESRI specific type that contains index and column type information. Two
** helper functions below initialize and free that structure member which is used locally by layer
** specific functions.
*/
static int layerInitItemInfo(layerObj *layer) 
{
  shapefileObj *shpfile=NULL;

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):    
    shpfile = layer->layerinfo;

    if(!shpfile) {
      msSetError(MS_SDEERR, "Shapefile layer has not been opened.", "layerInitItemInfo()");
      return(MS_FAILURE);
    }

    /* iteminfo needs to be a bit more complex, a list of indexes plus the length of the list */
    layer->iteminfo = (int *) msDBFGetItemIndexes(shpfile->hDBF, layer->items, layer->numitems);
    if(!layer->iteminfo) return(MS_FAILURE);
    return(MS_SUCCESS);
    break;
  case(MS_TILED_SHAPEFILE):
    /* iteminfo needs to be a bit more complex, a list of indexes plus the length of the list */
    return(msTiledSHPLayerInitItemInfo(layer));
    break;
  case(MS_INLINE):
    return(MS_SUCCESS); /* inline shapes have no items */
    break;
  case(MS_OGR):
    return(msOGRLayerInitItemInfo(layer));
    break;
  case(MS_WFS):
    return(msWFSLayerInitItemInfo(layer));
    break;
  case(MS_POSTGIS):
    return(msPOSTGISLayerInitItemInfo(layer));
    break;
  case(MS_MYGIS):
    return(msMYGISLayerInitItemInfo(layer));
    break;
  case(MS_SDE):
    return(msSDELayerInitItemInfo(layer));
    break;
  case(MS_ORACLESPATIAL):
    return(msOracleSpatialLayerInitItemInfo(layer));
    break;
  case(MS_GRATICULE):
    return(msGraticuleLayerInitItemInfo(layer));
    break;
  case(MS_RASTER):
    return(msRASTERLayerInitItemInfo(layer));
    break;
  default:
    break;
  }

  return(MS_FAILURE);
}

static void layerFreeItemInfo(layerObj *layer) 
{
  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):
  case(MS_TILED_SHAPEFILE):
    if(layer->iteminfo) free(layer->iteminfo);
    layer->iteminfo = NULL;
    break;
  case(MS_INLINE):
    break;
  case(MS_OGR):
  case(MS_WFS):
    msOGRLayerFreeItemInfo(layer);
    break;
  case(MS_POSTGIS):
    msPOSTGISLayerFreeItemInfo(layer);
    break;
  case(MS_MYGIS):
    msMYGISLayerFreeItemInfo(layer);
    break;
  case(MS_SDE):
    msSDELayerFreeItemInfo(layer);
    break;
  case(MS_ORACLESPATIAL):
    msOracleSpatialLayerFreeItemInfo(layer);
    break;
  case(MS_GRATICULE):
    msGraticuleLayerFreeItemInfo(layer);
    break;
  case(MS_RASTER):
    msRASTERLayerFreeItemInfo(layer);
    break;
  default:
    break;
  }

  return;
}

/*
** Does exactly what it implies, readies a layer for processing.
*/
int msLayerOpen(layerObj *layer)
{
  char szPath[MS_MAXPATHLEN];
  shapefileObj *shpfile;

  if(layer->features && layer->connectiontype != MS_GRATICULE ) 
    layer->connectiontype = MS_INLINE;

  if(layer->tileindex && layer->connectiontype == MS_SHAPEFILE)
    layer->connectiontype = MS_TILED_SHAPEFILE;

  if(layer->type == MS_LAYER_RASTER )
    layer->connectiontype = MS_RASTER;

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):
    if(layer->layerinfo) return(MS_SUCCESS); /* layer already open */

    /* allocate space for a shapefileObj using layer->layerinfo	 */
    shpfile = (shapefileObj *) malloc(sizeof(shapefileObj));
    if(!shpfile) {
      msSetError(MS_MEMERR, "Error allocating shapefileObj structure.", "msLayerOpen()");
      return(MS_FAILURE);
    }

    layer->layerinfo = shpfile;

    if(msSHPOpenFile(shpfile, "rb", msBuildPath3(szPath, layer->map->mappath, layer->map->shapepath, layer->data)) == -1)
      if(msSHPOpenFile(shpfile, "rb", msBuildPath(szPath, layer->map->mappath, layer->data)) == -1)
      {
          layer->layerinfo = NULL;
          free(shpfile);
          return(MS_FAILURE);
      }
   
    return(MS_SUCCESS);
    break;
  case(MS_TILED_SHAPEFILE):
    return(msTiledSHPOpenFile(layer));
    break;
  case(MS_INLINE):
    layer->currentfeature = layer->features; /* point to the begining of the feature list */
    return(MS_SUCCESS);
    break;
  case(MS_OGR):
    return(msOGRLayerOpen(layer,NULL));
    break;
  case(MS_WFS):
    return(msWFSLayerOpen(layer, NULL, NULL));
    break;
  case(MS_POSTGIS):
    return(msPOSTGISLayerOpen(layer));
    break;
  case(MS_MYGIS):
    return(msMYGISLayerOpen(layer));
    break;
  case(MS_SDE):
    return(msSDELayerOpen(layer));
    break;
  case(MS_ORACLESPATIAL):
    return(msOracleSpatialLayerOpen(layer));
    break;
  case(MS_GRATICULE):
    return(msGraticuleLayerOpen(layer));
    break;
  case(MS_RASTER):
    return(msRASTERLayerOpen(layer));
    break;
  default:
    break;
  }

  return(MS_FAILURE);
}

/*
** Returns MS_TRUE if layer has been opened using msLayerOpen(), MS_FALSE otherwise
*/
int msLayerIsOpen(layerObj *layer)
{

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):
  case(MS_TILED_SHAPEFILE):
    if(layer->layerinfo)
        return(MS_TRUE);
    else
        return(MS_FALSE);
    break;
  case(MS_INLINE):
    if (layer->currentfeature)
        return(MS_TRUE);
    else
        return(MS_FALSE);
    break;
  case(MS_OGR):
    return(msOGRLayerIsOpen(layer));
    break;
  case(MS_WFS):
    return(msWFSLayerIsOpen(layer));
    break;
  case(MS_POSTGIS):
    return(msPOSTGISLayerIsOpen(layer));
    break;
  case(MS_MYGIS):
    return(msMYGISLayerIsOpen(layer));
    break;
  case(MS_SDE):
    return(msSDELayerIsOpen(layer));
    break;
  case(MS_ORACLESPATIAL):
    return(msOracleSpatialLayerIsOpen(layer));
    break;
  case(MS_GRATICULE):
    return(msGraticuleLayerIsOpen(layer));
    break;
  case(MS_RASTER):
    return(msRASTERLayerIsOpen(layer));
    break;
  default:
    break;
  }

  return(MS_FALSE);
}

/*
** Performs a spatial, and optionally an attribute based feature search. The function basically
** prepares things so that candidate features can be accessed by query or drawing functions. For
** OGR and shapefiles this sets an internal bit vector that indicates whether a particular feature
** is to processed. For SDE it executes an SQL statement on the SDE server. Once run the msLayerNextShape
** function should be called to actually access the shapes.
**
** Note that for shapefiles we apply any maxfeatures constraint at this point. That may be the only
** connection type where this is feasible.
*/
int msLayerWhichShapes(layerObj *layer, rectObj rect)
{
  int i, n1=0, n2=0;
  int status;
  shapefileObj *shpfile;

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):    
    shpfile = layer->layerinfo;

    if(!shpfile) {
      msSetError(MS_SDEERR, "Shapefile layer has not been opened.", "msLayerWhichShapes()");
      return(MS_FAILURE);
    }

    status = msSHPWhichShapes(shpfile, rect, layer->debug);
    if(status != MS_SUCCESS) return(status);

    /* now apply the maxshapes criteria (NOTE: this ignores the filter so you could get less than maxfeatures) */
    if(layer->maxfeatures > 0) {
      for(i=0; i<shpfile->numshapes; i++)
        n1 += msGetBit(shpfile->status,i);
    
      if(n1 > layer->maxfeatures) {
        for(i=0; i<shpfile->numshapes; i++) {
          if(msGetBit(shpfile->status,i) && (n2 < (n1 - layer->maxfeatures))) {
	    msSetBit(shpfile->status,i,0);
	    n2++;
          }
        }
      }
    }

    return(MS_SUCCESS);
    break;
  case(MS_TILED_SHAPEFILE):
    return(msTiledSHPWhichShapes(layer, rect));
    break;
  case(MS_INLINE):
    return(MS_SUCCESS);
    break;
  case(MS_OGR):
    return(msOGRLayerWhichShapes(layer, rect));
    break;
  case(MS_WFS):
    return(msWFSLayerWhichShapes(layer, rect));
    break;
  case(MS_POSTGIS):
    return(msPOSTGISLayerWhichShapes(layer, rect));
    break;
  case(MS_MYGIS):
    return(msMYGISLayerWhichShapes(layer, rect));
    break;
  case(MS_SDE):
    return(msSDELayerWhichShapes(layer, rect));
    break;
  case(MS_ORACLESPATIAL):
    return(msOracleSpatialLayerWhichShapes(layer, rect));
    break;
  case(MS_GRATICULE):
    return(msGraticuleLayerWhichShapes(layer, rect));
    break;
  case(MS_RASTER):
    return(msRASTERLayerWhichShapes(layer, rect));
    break;
  default:
    break;
  }

  return(MS_FAILURE);
}

/*
** Called after msWhichShapes has been called to actually retrieve shapes within a given area
** and matching a vendor specific filter (i.e. layer FILTER attribute).
**
** Shapefiles: NULL shapes (shapes with attributes but NO vertices are skipped)
*/
int msLayerNextShape(layerObj *layer, shapeObj *shape) 
{
  int i, filter_passed;
  char **values=NULL;
  shapefileObj *shpfile;


  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):    
    shpfile = layer->layerinfo;

    if(!shpfile) {
      msSetError(MS_SDEERR, "Shapefile layer has not been opened.", "msLayerNextShape()");
      return(MS_FAILURE);
    }

    do {
      i = shpfile->lastshape + 1;
      while(i<shpfile->numshapes && !msGetBit(shpfile->status,i)) i++; /* next "in" shape */
      shpfile->lastshape = i;

      if(i == shpfile->numshapes) return(MS_DONE); /* nothing else to read */

      filter_passed = MS_TRUE;  /* By default accept ANY shape */
      if(layer->numitems > 0 && layer->iteminfo) {
        values = msDBFGetValueList(shpfile->hDBF, i, layer->iteminfo, layer->numitems);
        if(!values) return(MS_FAILURE);
        if ((filter_passed = msEvalExpression(&(layer->filter), layer->filteritemindex, values, layer->numitems)) != MS_TRUE) {
            msFreeCharArray(values, layer->numitems);
            values = NULL;
        }
      }
    } while(!filter_passed);  /* Loop until both spatial and attribute filters match */

    msSHPReadShape(shpfile->hSHP, i, shape); /* ok to read the data now */

    /* skip NULL shapes (apparently valid for shapefiles, at least ArcView doesn't care) */
    if(shape->type == MS_SHAPE_NULL) return(msLayerNextShape(layer, shape));

    shape->values = values;
    shape->numvalues = layer->numitems;
    return(MS_SUCCESS);
    break;
  case(MS_TILED_SHAPEFILE):
    return(msTiledSHPNextShape(layer, shape));
  case(MS_INLINE):
    if(!(layer->currentfeature)) return(MS_DONE); /* out of features     */
    msCopyShape(&(layer->currentfeature->shape), shape);
    layer->currentfeature = layer->currentfeature->next;
    return(MS_SUCCESS);
    break;
  case(MS_OGR):
  case(MS_WFS):
    return(msOGRLayerNextShape(layer, shape));
    break;
  case(MS_POSTGIS):
    return(msPOSTGISLayerNextShape(layer, shape));
    break;
  case(MS_MYGIS):
    return(msMYGISLayerNextShape(layer, shape));
    break;
  case(MS_SDE):
    return(msSDELayerNextShape(layer, shape));
    break;
  case(MS_ORACLESPATIAL):
    return(msOracleSpatialLayerNextShape(layer, shape));
    break;
  case(MS_GRATICULE):
    return(msGraticuleLayerNextShape(layer, shape));
    break;
  case(MS_RASTER):
    return(msRASTERLayerNextShape(layer, shape));
    break;
  default:
    break;
  }

  /* TO DO! This is where dynamic joins will happen. Joined attributes will be */
  /* tagged on to the main attributes with the naming scheme [join name].[item name]. */
  /* We need to leverage the iteminfo (I think) at this point */

  return(MS_FAILURE);
}

/*
** Used to retrieve a shape by index. All data sources must be capable of random access using
** a record number of some sort.
*/
int msLayerGetShape(layerObj *layer, shapeObj *shape, int tile, long record)
{
  shapefileObj *shpfile;

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):    
    shpfile = layer->layerinfo;

    if(!shpfile) {
      msSetError(MS_SDEERR, "Shapefile layer has not been opened.", "msLayerGetShape()");
      return(MS_FAILURE);
    }

    /* msSHPReadShape *should* return success or failure so we don't have to test here */
    if(record < 0 || record >= shpfile->numshapes) {
      msSetError(MS_MISCERR, "Invalid feature id.", "msLayerGetShape()");
      return(MS_FAILURE);
    }

    msSHPReadShape(shpfile->hSHP, record, shape);
    if(layer->numitems > 0 && layer->iteminfo) {
      shape->numvalues = layer->numitems;
      shape->values = msDBFGetValueList(shpfile->hDBF, record, layer->iteminfo, layer->numitems);
      if(!shape->values) return(MS_FAILURE);
    }
    return(MS_SUCCESS);
    break;
  case(MS_TILED_SHAPEFILE):
    return(msTiledSHPGetShape(layer, shape, tile, record));
  case(MS_INLINE):
    return msINLINELayerGetShape(layer, shape, record);
    /* msSetError(MS_MISCERR, "Cannot retrieve inline shapes randomly.", "msLayerGetShape()"); */
    /* return(MS_FAILURE); */
    break;
  case(MS_OGR):
  case(MS_WFS):
    return(msOGRLayerGetShape(layer, shape, tile, record));
    break;
  case(MS_POSTGIS):
    return(msPOSTGISLayerGetShape(layer, shape, record));
    break;
  case(MS_MYGIS):
    return(msMYGISLayerGetShape(layer, shape, record));
    break;
  case(MS_SDE):
    return(msSDELayerGetShape(layer, shape, record));
    break;
  case(MS_ORACLESPATIAL):
    return(msOracleSpatialLayerGetShape(layer, shape, record));
    break;
  case(MS_GRATICULE):
    return(msGraticuleLayerGetShape(layer, shape, tile, record));
    break;
  case(MS_RASTER):
    return(msRASTERLayerGetShape(layer, shape, tile, record));
    break;
  default:
    break;
  }

  /* TO DO! This is where dynamic joins will happen. Joined attributes will be */
  /* tagged on to the main attributes with the naming scheme [join name].[item name]. */

  return(MS_FAILURE);
}

/*
** Closes resources used by a particular layer.
*/
void msLayerClose(layerObj *layer) 
{
  shapefileObj *shpfile;

  /* no need for items once the layer is closed */
  layerFreeItemInfo(layer);
  if(layer->items) {
    msFreeCharArray(layer->items, layer->numitems);
    layer->items = NULL;
    layer->numitems = 0;
  }

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):
    shpfile = layer->layerinfo;
    if(!shpfile) return; /* nothing to do */
    msSHPCloseFile(shpfile);
    free(layer->layerinfo);
    layer->layerinfo = NULL;
    break;
  case(MS_TILED_SHAPEFILE):
    msTiledSHPClose(layer);
    break;
  case(MS_INLINE):
    break;
  case(MS_OGR):
    msOGRLayerClose(layer);
    break;
  case(MS_WFS):
    msWFSLayerClose(layer);
    break;
  case(MS_POSTGIS):
    msPOSTGISLayerClose(layer);
    break;
  case(MS_MYGIS):
    msMYGISLayerClose(layer);
    break;
  case(MS_SDE):
    /* using pooled connections for SDE, closed when map file is closed */
    /* msSDELayerClose(layer);  */
    break;
  case(MS_ORACLESPATIAL):
    msOracleSpatialLayerClose(layer);
    break;
  case(MS_GRATICULE):
    msGraticuleLayerClose(layer);
    break;
  case(MS_RASTER):
    msRASTERLayerClose(layer);
    break;
  default:
    break;
  }
}

/*
** Retrieves a list of attributes available for this layer. Most sources also set the iteminfo array
** at this point. This function is used when processing query results to expose attributes to query
** templates. At that point all attributes are fair game.
*/
int msLayerGetItems(layerObj *layer) 
{
  shapefileObj *shpfile;

  /* clean up any previously allocated instances */
  layerFreeItemInfo(layer);
  if(layer->items) {
    msFreeCharArray(layer->items, layer->numitems);
    layer->items = NULL;
    layer->numitems = 0;
  }

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):
    shpfile = layer->layerinfo;

    if(!shpfile) {
      msSetError(MS_SDEERR, "Shapefile layer has not been opened.", "msLayerGetItems()");
      return(MS_FAILURE);
    }

    layer->numitems = msDBFGetFieldCount(shpfile->hDBF);
    layer->items = msDBFGetItems(shpfile->hDBF);    
    if(!layer->items) return(MS_FAILURE);
    layerInitItemInfo(layer);
    return(MS_SUCCESS);
    break;
  case(MS_TILED_SHAPEFILE):    
    return(msTiledSHPLayerGetItems(layer));
    break;
  case(MS_INLINE):
    return(MS_SUCCESS); /* inline shapes have no items */
    break;
  case(MS_OGR):
    return(msOGRLayerGetItems(layer));
    break;
  case(MS_WFS):
    return(msWFSLayerGetItems(layer));
    break;
  case(MS_POSTGIS):
    return(msPOSTGISLayerGetItems(layer));
    break;
  case(MS_MYGIS):
    return(msMYGISLayerGetItems(layer));
    break;
  case(MS_SDE):
    return(msSDELayerGetItems(layer));
    break;
  case(MS_ORACLESPATIAL):
    return(msOracleSpatialLayerGetItems(layer));
    break;
  case(MS_GRATICULE):
    return(msGraticuleLayerGetItems(layer));
    break;
  case(MS_RASTER):
    return(msRASTERLayerGetItems(layer));
    break;
  default:
    break;
  }

  /* TO DO! Need to add any joined itemd on to the core layer items, one long list!  */

  return(MS_FAILURE);
}

/*
** Returns extent of spatial coverage for a layer.
**
** If layer->extent is set then this value is used, otherwise the 
** driver-specific implementation is called (this can be expensive).
**
** If layer is not already opened then it is opened and closed (so this
** function can be called on both opened or closed layers).
**
** Returns MS_SUCCESS/MS_FAILURE.
*/
int msLayerGetExtent(layerObj *layer, rectObj *extent) 
{
  int need_to_close = MS_FALSE, status = MS_SUCCESS;

  if (MS_VALID_EXTENT(layer->extent))
  {
      *extent = layer->extent;
      return MS_SUCCESS;
  }

  if (!msLayerIsOpen(layer))
  {
      if (msLayerOpen(layer) != MS_SUCCESS)
          return MS_FAILURE;
      need_to_close = MS_TRUE;
  }

  switch(layer->connectiontype) {
  case(MS_SHAPEFILE):
    *extent = ((shapefileObj*)layer->layerinfo)->bounds;
    status = MS_SUCCESS;
    break;
  case(MS_TILED_SHAPEFILE):
    status = msTiledSHPLayerGetExtent(layer, extent);
    break;
  case(MS_INLINE):
    /* __TODO__ need to compute extents */
    status = MS_FAILURE;
    break;
  case(MS_OGR):
  case(MS_WFS):
    status = msOGRLayerGetExtent(layer, extent);
    break;
  case(MS_POSTGIS):
    status = msPOSTGISLayerGetExtent(layer, extent);
    break;
  case(MS_MYGIS):
    status = msMYGISLayerGetExtent(layer, extent);
    break;
  case(MS_SDE):
    status = msSDELayerGetExtent(layer, extent);
    break;
  case(MS_ORACLESPATIAL):
    status = msOracleSpatialLayerGetExtent(layer, extent);
    break;
  case(MS_GRATICULE):
    status = msGraticuleLayerGetExtent(layer, extent);
    break;
  case(MS_RASTER):
    status = msRASTERLayerGetExtent(layer, extent);
    break;
  default:
    break;
  }

  if (need_to_close)
      msLayerClose(layer);

  return(status);
}

static int string2list(char **list, int *listsize, char *string)
{
  int i;

  for(i=0; i<(*listsize); i++)
    if(strcmp(list[i], string) == 0) { 
        /* printf("string2list (duplicate): %s %d\n", string, i); */
      return(i);
    }

  list[i] = strdup(string);
  (*listsize)++;

  /* printf("string2list: %s %d\n", string, i); */

  return(i);
}

/* TO DO: this function really needs to use the lexer */
static void expression2list(char **list, int *listsize, expressionObj *expression) 
{
  int i, j, l;
  char tmpstr1[1024], tmpstr2[1024];
  short in=MS_FALSE;
  int tmpint;

  j = 0;
  l = strlen(expression->string);
  for(i=0; i<l; i++) {
    if(expression->string[i] == '[') {
      in = MS_TRUE;
      tmpstr2[j] = expression->string[i];
      j++;
      continue;
    }
    if(expression->string[i] == ']') {
      in = MS_FALSE;

      tmpint = expression->numitems;

      tmpstr2[j] = expression->string[i];
      tmpstr2[j+1] = '\0';
      string2list(expression->items, &(expression->numitems), tmpstr2);

      if(tmpint != expression->numitems) { /* not a duplicate, so no need to calculate the index */
        tmpstr1[j-1] = '\0';
        expression->indexes[expression->numitems - 1] = string2list(list, listsize, tmpstr1);
      }

      j = 0; /* reset */

      continue;
    }

    if(in) {
      tmpstr2[j] = expression->string[i];
      tmpstr1[j-1] = expression->string[i];
      j++;
    }
  }
}

int msLayerWhichItemsNew(layerObj *layer, int classify, int annotate, char *metadata) 
{
  int status;
  int numchars;
/* int i; */

  status = msLayerGetItems(layer); /* get a list of all attributes available for this layer (including JOINs) */
  if(status != MS_SUCCESS) return(status);

  /* allocate space for the various item lists */
  if(classify && layer->filter.type == MS_EXPRESSION) { 
    numchars = countChars(layer->filter.string, '[');
    if(numchars > 0) {
      layer->filter.items = (char **)calloc(numchars, sizeof(char *)); /* should be more than enough space */
      if(!(layer->filter.items)) {
	msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	return(MS_FAILURE);
      }
      layer->filter.indexes = (int *)malloc(numchars*sizeof(int));
      if(!(layer->filter.indexes)) {
	msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	return(MS_FAILURE);
      }
      layer->filter.numitems = 0;     
    }
  }

  /* for(i=0; i<layer->numitems; i++) { } */

  return(MS_SUCCESS);
}

/*
** This function builds a list of items necessary to draw or query a particular layer by
** examining the contents of the various xxxxitem parameters and expressions. That list is
** then used to set the iteminfo variable. SDE is kinda special, we always need to retrieve
** the shape column and a virtual record number column. Most sources should not have to 
** modify this function.
*/
int msLayerWhichItems(layerObj *layer, int classify, int annotate, char *metadata) 
{
  int i, j;
  int nt=0, ne=0;
  
  /*  */
  /* TO DO! I have a better algorithm. */
  /*  */
  /* 1) call msLayerGetItems to get a complete list (including joins) */
  /* 2) loop though item-based parameters and expressions to identify items to keep */
  /* 3) based on 2) build a list of items */
  /*  */
  /* This is more straight forward and robust, fixes the problem of [...] regular expressions */
  /* embeded in logical expressions. It also opens up to using dynamic joins anywhere... */
  /*  */

  /* Cleanup any previous item selection */
  layerFreeItemInfo(layer);
  if(layer->items) {
    msFreeCharArray(layer->items, layer->numitems);
    layer->items = NULL;
    layer->numitems = 0;
  }

  if(classify) {
    if(layer->classitem) nt++;
    if(layer->filteritem) nt++;

    for(i=0; i<layer->numclasses; i++) {
      for(j=0; j<layer->class[i].numstyles; j++) {
        if(layer->class[i].styles[j].angleitem) nt++;
	if(layer->class[i].styles[j].sizeitem) nt++;
	if(layer->class[i].styles[j].rangeitem) nt++;
      }
    }

    ne = 0;
    if(layer->filter.type == MS_EXPRESSION) {
      ne = countChars(layer->filter.string, '[');
      if(ne > 0) {
	layer->filter.items = (char **)calloc(ne, sizeof(char *)); /* should be more than enough space */
	if(!(layer->filter.items)) {
	  msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	  return(MS_FAILURE);
	}
	layer->filter.indexes = (int *)malloc(ne*sizeof(int));
	if(!(layer->filter.indexes)) {
	  msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	  return(MS_FAILURE);
	}
	layer->filter.numitems = 0;
	nt += ne;
      }
    }
  }

  if(annotate) {
    if(layer->labelitem) nt++;
    if(layer->labelsizeitem) nt++;
    if(layer->labelangleitem) nt++;
  }

  for(i=0; i<layer->numclasses; i++) {
    ne = 0;
    if(classify && layer->class[i].expression.type == MS_EXPRESSION) { 
      ne = countChars(layer->class[i].expression.string, '[');
      if(ne > 0) {
	layer->class[i].expression.items = (char **)calloc(ne, sizeof(char *)); /* should be more than enough space */
	if(!(layer->class[i].expression.items)) {
	  msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	  return(MS_FAILURE);
	}
	layer->class[i].expression.indexes = (int *)malloc(ne*sizeof(int));
	if(!(layer->class[i].expression.indexes)) {
	  msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	  return(MS_FAILURE);
	}
	layer->class[i].expression.numitems = 0;
	nt += ne;
      }
    }

    ne = 0;
    if(annotate && layer->class[i].text.type == MS_EXPRESSION) { 
      ne = countChars(layer->class[i].text.string, '[');
      if(ne > 0) {
	layer->class[i].text.items = (char **)calloc(ne, sizeof(char *)); /* should be more than enough space */
	if(!(layer->class[i].text.items)) {
	  msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	  return(MS_FAILURE);
	}
	layer->class[i].text.indexes = (int *)malloc(ne*sizeof(int));
	if(!(layer->class[i].text.indexes)) {
	  msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
	  return(MS_FAILURE);
	}
	layer->class[i].text.numitems = 0;
	nt += ne;
      }
    }
  }

  /* TODO: it would be nice to move this into the SDE code itself, feels wrong here... */
  if(layer->connectiontype == MS_SDE) {
    layer->items = (char **)calloc(nt+2, sizeof(char *)); /* should be more than enough space, SDE always needs a couple of additional items */
    if(!layer->items) {
      msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
      return(MS_FAILURE);
    }
    layer->items[0] = msSDELayerGetRowIDColumn(layer); /* row id */
    layer->items[1] = msSDELayerGetSpatialColumn(layer);
    layer->numitems = 2;
  } else {
    /* if(nt == 0) return(MS_SUCCESS); */
    if(nt > 0) {
      layer->items = (char **)calloc(nt, sizeof(char *)); /* should be more than enough space */
      if(!layer->items) {
        msSetError(MS_MEMERR, NULL, "msLayerWhichItems()");
        return(MS_FAILURE);
      }
      layer->numitems = 0;
    }
  }

  if(nt > 0) {
    if(classify) {
      if(layer->classitem) layer->classitemindex = string2list(layer->items, &(layer->numitems), layer->classitem);
      if(layer->filteritem) layer->filteritemindex = string2list(layer->items, &(layer->numitems), layer->filteritem);

      for(i=0; i<layer->numclasses; i++) {
	for(j=0; j<layer->class[i].numstyles; j++) {
	  if(layer->class[i].styles[j].angleitem) layer->class[i].styles[j].angleitemindex = string2list(layer->items, &(layer->numitems), layer->class[i].styles[j].angleitem);
	  if(layer->class[i].styles[j].sizeitem) layer->class[i].styles[j].sizeitemindex = string2list(layer->items, &(layer->numitems), layer->class[i].styles[j].sizeitem); 
	  if(layer->class[i].styles[j].rangeitem) layer->class[i].styles[j].rangeitemindex = string2list(layer->items, &(layer->numitems), layer->class[i].styles[j].rangeitem); 
	}
      }

      if(layer->filter.type == MS_EXPRESSION) expression2list(layer->items, &(layer->numitems), &(layer->filter));      
    }
    if(annotate) {
      if(layer->labelitem) layer->labelitemindex = string2list(layer->items, &(layer->numitems), layer->labelitem);
      if(layer->labelsizeitem) layer->labelsizeitemindex = string2list(layer->items, &(layer->numitems), layer->labelsizeitem);
      if(layer->labelangleitem) layer->labelangleitemindex = string2list(layer->items, &(layer->numitems), layer->labelangleitem);
    }  

    for(i=0; i<layer->numclasses; i++) {
      if(classify && layer->class[i].expression.type == MS_EXPRESSION) expression2list(layer->items, &(layer->numitems), &(layer->class[i].expression));
      if(annotate && layer->class[i].text.type == MS_EXPRESSION) expression2list(layer->items, &(layer->numitems), &(layer->class[i].text));
    }
  }

  if(metadata) {
    char **tokens;
    int n = 0;
    int j;
    int bFound = 0;
      
    tokens = split(metadata, ',', &n);
    if(tokens) {
      for(i=0; i<n; i++) {
        bFound = 0;
        for(j=0; j<layer->numitems; j++) {
          if(strcmp(tokens[i], layer->items[j]) == 0) {
            bFound = 1;
            break;
          }
        }
              
        if(!bFound) {
          layer->numitems++;
          layer->items =  (char **)realloc(layer->items, sizeof(char *)*(layer->numitems));
          layer->items[layer->numitems-1] = strdup(tokens[i]);
        }
      }
      msFreeCharArray(tokens, n);
    }
  }

  /* populate the iteminfo array */
  if(layer->numitems == 0)
    return(MS_SUCCESS);

  return(layerInitItemInfo(layer));
}

/*
** A helper function to set the items to be retrieved with a particular shape. Unused at the moment but will be used
** from within MapScript. Should not need modification.
*/
int msLayerSetItems(layerObj *layer, char **items, int numitems)
{
  int i;
  /* Cleanup any previous item selection */
  layerFreeItemInfo(layer);
  if(layer->items) {
    msFreeCharArray(layer->items, layer->numitems);
    layer->items = NULL;
    layer->numitems = 0;
  }

  /* now allocate and set the layer item parameters  */
  layer->items = (char **)malloc(sizeof(char *)*numitems);
  if(!layer->items) {
    msSetError(MS_MEMERR, NULL, "msLayerSetItems()");
    return(MS_FAILURE);
  }

  for(i=0; i<numitems; i++)
    layer->items[i] = strdup(items[i]);
  layer->numitems = numitems;

  /* populate the iteminfo array */
  return(layerInitItemInfo(layer));

  return(MS_SUCCESS);
}

/*
** Fills a classObj with style info from the specified shape.  This is used
** with STYLEITEM AUTO when rendering shapes.
** For optimal results, this should be called immediately after 
** GetNextShape() or GetShape() so that the shape doesn't have to be read
** twice.
** 
*/
int msLayerGetAutoStyle(mapObj *map, layerObj *layer, classObj *c, int tile, long record)
{
  switch(layer->connectiontype) {
  case(MS_OGR):
    return(msOGRLayerGetAutoStyle(map, layer, c, tile, record));
    break;
  case(MS_SHAPEFILE):
  case(MS_TILED_SHAPEFILE):
  case(MS_INLINE):
  case(MS_POSTGIS):
  case(MS_MYGIS):
  case(MS_SDE):
  case(MS_ORACLESPATIAL):
  case(MS_WFS):
  case(MS_GRATICULE):
  case(MS_RASTER):
  default:
    msSetError(MS_MISCERR, "'STYLEITEM AUTO' not supported for this data source.", "msLayerGetAutoStyle()");
    return(MS_FAILURE);    
    break;
  }

  return(MS_FAILURE);
}

/* Author: Cristoph Spoerri and Sean Gillies */
int msINLINELayerGetShape(layerObj *layer, shapeObj *shape, int shapeindex) {
    int i=0;
    featureListNodeObjPtr current;

    current = layer->features;
    while (current!=NULL && i!=shapeindex) {
        i++;
        current = current->next;
    }
    if (current == NULL) {
        msSetError(MS_SHPERR, "No inline feature with this index.",
                   "msINLINELayerGetShape()");
        return MS_FAILURE;
    } 
    
    if (msCopyShape(&(current->shape), shape) != MS_SUCCESS) {
        msSetError(MS_SHPERR, "Cannot retrieve inline shape. There some problem with the shape", "msLayerGetShape()");
        return MS_FAILURE;
    }
    return MS_SUCCESS;
}

/*
Returns the number of inline feature of a layer
*/
int msLayerGetNumFeatures(layerObj *layer) {
    int i = 0;
    featureListNodeObjPtr current;
    if (layer->connectiontype==MS_INLINE) {
        current = layer->features;
        while (current!=NULL) {
            i++;
            current = current->next;
        }
    }
    else {
        msSetError(MS_SHPERR, "Not an inline layer", "msLayerGetNumFeatures()");
        return MS_FAILURE;
    }
    return i;
}

void 
msLayerSetProcessingKey( layerObj *layer, const char *key, const char *value)

{
    int len = strlen(key);
    int i;
    char *directive;

    directive = (char *) malloc(strlen(key)+strlen(value)+2);
    sprintf( directive, "%s=%s", key, value );

    for( i = 0; i < layer->numprocessing; i++ )
    {
        /* If the key is found, replace it */
        if( strncasecmp( key, layer->processing[i], len ) == 0 
            && layer->processing[i][len] == '=' )
        {
            free( layer->processing[i] );
            layer->processing[i] = directive;
            return;
        }
    }

    /* otherwise add the directive at the end. */

    msLayerAddProcessing( layer, directive );
    free( directive );
}

void msLayerAddProcessing( layerObj *layer, const char *directive )

{
    layer->numprocessing++;
    if( layer->numprocessing == 1 )
        layer->processing = (char **) malloc(2*sizeof(char *));
    else
        layer->processing = (char **) realloc(layer->processing, sizeof(char*) * (layer->numprocessing+1) );
    layer->processing[layer->numprocessing-1] = strdup(directive);
    layer->processing[layer->numprocessing] = NULL;
}

char *msLayerGetProcessing( layerObj *layer, int proc_index) {
    if (proc_index < 0 || proc_index >= layer->numprocessing) {
        msSetError(MS_CHILDERR, "Invalid processing index.", "msLayerGetProcessing()");
        return NULL;
    }
    else {
        return layer->processing[proc_index];
    }
}

char *msLayerGetProcessingKey( layerObj *layer, const char *key ) 
{
    int i, len = strlen(key);

    for( i = 0; i < layer->numprocessing; i++ )
    {
        if( strncasecmp(layer->processing[i],key,len) == 0 
            && layer->processing[i][len] == '=' )
            return layer->processing[i] + len + 1;
    }
    
    return NULL;
}

int msLayerClearProcessing( layerObj *layer ) {
    if (layer->numprocessing > 0) {
        msFreeCharArray( layer->processing, layer->numprocessing );
        layer->processing = NULL;
        layer->numprocessing = 0;
    }
    return layer->numprocessing;
}

int msPOSTGISLayerSetTimeFilter(layerObj *lp, 
                                const char *timestring, 
                                const char *timefield)
{
    char *tmpstimestring = NULL;
    char *timeresolution = NULL;
    int timesresol = -1;
    char **atimes, **tokens = NULL;
    int numtimes=0,i=0,ntmp=0,nlength=0;
    char buffer[512];

    buffer[0] = '\0';

    if (!lp || !timestring || !timefield)
      return MS_FALSE;

    if (strstr(timestring, ",") == NULL && 
        strstr(timestring, "/") == NULL) /* discrete time */
      tmpstimestring = strdup(timestring);
    else
    {
        atimes = split (timestring, ',', &numtimes);
        if (atimes == NULL || numtimes < 1)
          return MS_FALSE;

        if (numtimes >= 1)
        {
            tokens = split(atimes[0],  '/', &ntmp);
            if (ntmp == 2) /* ranges */
            {
                tmpstimestring = strdup(tokens[0]);
                msFreeCharArray(tokens, ntmp);
            }
            else if (ntmp == 1) /* multiple times */
            {
                tmpstimestring = strdup(atimes[0]);
            }
        }
        msFreeCharArray(atimes, numtimes);
    }
    if (!tmpstimestring)
      return MS_FALSE;
        
    timesresol = msTimeGetResolution((const char*)tmpstimestring);
    if (timesresol < 0)
      return MS_FALSE;

    free(tmpstimestring);

    switch (timesresol)
    {
        case (TIME_RESOLUTION_SECOND):
          timeresolution = strdup("second");
          break;

        case (TIME_RESOLUTION_MINUTE):
          timeresolution = strdup("minute");
          break;

        case (TIME_RESOLUTION_HOUR):
          timeresolution = strdup("hour");
          break;

        case (TIME_RESOLUTION_DAY):
          timeresolution = strdup("day");
          break;

        case (TIME_RESOLUTION_MONTH):
          timeresolution = strdup("month");
          break;

        case (TIME_RESOLUTION_YEAR):
          timeresolution = strdup("year");
          break;

        default:
          break;
    }

    if (!timeresolution)
      return MS_FALSE;

    /* where date_trunc('month', _cwctstamp) = '2004-08-01' */
    if (strstr(timestring, ",") == NULL && 
        strstr(timestring, "/") == NULL) /* discrete time */
    {
        if(lp->filteritem) free(lp->filteritem);
        lp->filteritem = strdup(timefield);
        if (&lp->filter)
          freeExpression(&lp->filter);
        

        strcat(buffer, "(");

        strcat(buffer, "date_trunc('");
        strcat(buffer, timeresolution);
        strcat(buffer, "', ");        
        strcat(buffer, timefield);
        strcat(buffer, ")");        
        
         
        strcat(buffer, " = ");
        strcat(buffer,  "'");
        strcat(buffer, timestring);
        /* make sure that the timestring is complete and acceptable */
        /* to the date_trunc function : */
        /* - if the resolution is year (2004) or month (2004-01),  */
        /* a complete string for time would be 2004-01-01 */
        /* - if the resolluion is hour or minute (2004-01-01 15), a  */
        /* complete time is 2004-01-01 15:00:00 */
        if (strcasecmp(timeresolution, "year")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != '-')
              strcat(buffer,"-01-01");
            else
              strcat(buffer,"01-01");
        }            
        else if (strcasecmp(timeresolution, "month")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != '-')
              strcat(buffer,"-01");
            else
              strcat(buffer,"01");
        }            
        else if (strcasecmp(timeresolution, "hour")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != ':')
              strcat(buffer,":00:00");
            else
              strcat(buffer,"00:00");
        }            
        else if (strcasecmp(timeresolution, "minute")==0)
        {
            nlength = strlen(timestring);
            if (timestring[nlength-1] != ':')
              strcat(buffer,":00");
            else
              strcat(buffer,"00");
        }            
        

        strcat(buffer,  "'");

        strcat(buffer, ")");
        
        /* loadExpressionString(&lp->filter, (char *)timestring); */
        loadExpressionString(&lp->filter, buffer);

        free(timeresolution);
        return MS_TRUE;
    }
    
    atimes = split (timestring, ',', &numtimes);
    if (atimes == NULL || numtimes < 1)
      return MS_FALSE;

    if (numtimes >= 1)
    {
        /* check to see if we have ranges by parsing the first entry */
        tokens = split(atimes[0],  '/', &ntmp);
        if (ntmp == 2) /* ranges */
        {
            msFreeCharArray(tokens, ntmp);
            for (i=0; i<numtimes; i++)
            {
                tokens = split(atimes[i],  '/', &ntmp);
                if (ntmp == 2)
                {
                    if (strlen(buffer) > 0)
                      strcat(buffer, " OR ");
                    else
                      strcat(buffer, "(");

                    strcat(buffer, "(");
                    
                    strcat(buffer, "date_trunc('");
                    strcat(buffer, timeresolution);
                    strcat(buffer, "', ");        
                    strcat(buffer, timefield);
                    strcat(buffer, ")");        
 
                    strcat(buffer, " >= ");
                    
                    strcat(buffer,  "'");

                    strcat(buffer, tokens[0]);
                    /* - if the resolution is year (2004) or month (2004-01),  */
                    /* a complete string for time would be 2004-01-01 */
                    /* - if the resolluion is hour or minute (2004-01-01 15), a  */
                    /* complete time is 2004-01-01 15:00:00 */
                    if (strcasecmp(timeresolution, "year")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != '-')
                          strcat(buffer,"-01-01");
                        else
                          strcat(buffer,"01-01");
                    }            
                    else if (strcasecmp(timeresolution, "month")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != '-')
                          strcat(buffer,"-01");
                        else
                          strcat(buffer,"01");
                    }            
                    else if (strcasecmp(timeresolution, "hour")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != ':')
                          strcat(buffer,":00:00");
                        else
                          strcat(buffer,"00:00");
                    }            
                    else if (strcasecmp(timeresolution, "minute")==0)
                    {
                        nlength = strlen(tokens[0]);
                        if (tokens[0][nlength-1] != ':')
                          strcat(buffer,":00");
                        else
                          strcat(buffer,"00");
                    }            

                    strcat(buffer,  "'");
                    strcat(buffer, " AND ");

                    
                    strcat(buffer, "date_trunc('");
                    strcat(buffer, timeresolution);
                    strcat(buffer, "', ");        
                    strcat(buffer, timefield);
                    strcat(buffer, ")");  

                    strcat(buffer, " <= ");
                    
                    strcat(buffer,  "'");
                    strcat(buffer, tokens[1]);

                    /* - if the resolution is year (2004) or month (2004-01),  */
                    /* a complete string for time would be 2004-01-01 */
                    /* - if the resolluion is hour or minute (2004-01-01 15), a  */
                    /* complete time is 2004-01-01 15:00:00 */
                    if (strcasecmp(timeresolution, "year")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != '-')
                          strcat(buffer,"-01-01");
                        else
                          strcat(buffer,"01-01");
                    }            
                    else if (strcasecmp(timeresolution, "month")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != '-')
                          strcat(buffer,"-01");
                        else
                          strcat(buffer,"01");
                    }            
                    else if (strcasecmp(timeresolution, "hour")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != ':')
                          strcat(buffer,":00:00");
                        else
                          strcat(buffer,"00:00");
                    }            
                    else if (strcasecmp(timeresolution, "minute")==0)
                    {
                        nlength = strlen(tokens[1]);
                        if (tokens[1][nlength-1] != ':')
                          strcat(buffer,":00");
                        else
                          strcat(buffer,"00");
                    }            

                    strcat(buffer,  "'");
                    strcat(buffer, ")");
                }
                 
                msFreeCharArray(tokens, ntmp);
            }
            if (strlen(buffer) > 0)
              strcat(buffer, ")");
        }
        else if (ntmp == 1) /* multiple times */
        {
            msFreeCharArray(tokens, ntmp);
            strcat(buffer, "(");
            for (i=0; i<numtimes; i++)
            {
                if (i > 0)
                  strcat(buffer, " OR ");

                strcat(buffer, "(");
                  
                strcat(buffer, "date_trunc('");
                strcat(buffer, timeresolution);
                strcat(buffer, "', ");        
                strcat(buffer, timefield);
                strcat(buffer, ")");   

                strcat(buffer, " = ");
                  
                strcat(buffer,  "'");

                strcat(buffer, atimes[i]);
                
                /* make sure that the timestring is complete and acceptable */
                /* to the date_trunc function : */
                /* - if the resolution is year (2004) or month (2004-01),  */
                /* a complete string for time would be 2004-01-01 */
                /* - if the resolluion is hour or minute (2004-01-01 15), a  */
                /* complete time is 2004-01-01 15:00:00 */
                if (strcasecmp(timeresolution, "year")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != '-')
                      strcat(buffer,"-01-01");
                    else
                      strcat(buffer,"01-01");
                }            
                else if (strcasecmp(timeresolution, "month")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != '-')
                      strcat(buffer,"-01");
                    else
                      strcat(buffer,"01");
                }            
                else if (strcasecmp(timeresolution, "hour")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != ':')
                      strcat(buffer,":00:00");
                    else
                      strcat(buffer,"00:00");
                }            
                else if (strcasecmp(timeresolution, "minute")==0)
                {
                    nlength = strlen(atimes[i]);
                    if (atimes[i][nlength-1] != ':')
                      strcat(buffer,":00");
                    else
                      strcat(buffer,"00");
                }            

                strcat(buffer,  "'");
                strcat(buffer, ")");
            } 
            strcat(buffer, ")");
        }
        else
        {
            msFreeCharArray(atimes, numtimes);
            return MS_FALSE;
        }

        msFreeCharArray(atimes, numtimes);

        /* load the string to the filter */
        if (strlen(buffer) > 0)
        {
            if(lp->filteritem) 
              free(lp->filteritem);
            lp->filteritem = strdup(timefield);     
            if (&lp->filter)
              freeExpression(&lp->filter);
            loadExpressionString(&lp->filter, buffer);
        }

        free(timeresolution);
        return MS_TRUE;
                 
    }
    
    return MS_FALSE;
}
     
/**
  set the filter parameter for a time filter
**/

int msLayerSetTimeFilter(layerObj *lp, const char *timestring, 
                         const char *timefield) 
{
  
    char **atimes, **tokens = NULL;
    int numtimes,i, ntmp = 0;
    char buffer[512];
    int addtimebacktics = 0;


    buffer[0] = '\0';

    if (!lp || !timestring || !timefield)
      return MS_FALSE;

    if (lp->connectiontype == MS_POSTGIS)
    {
        return msPOSTGISLayerSetTimeFilter(lp,timestring, timefield);
    }

    /* for shape and ogr files time expressions are */
    /* delimited using backtics (ex `[TIME]` eq `2004-01-01`) */
    if (lp->connectiontype == MS_SHAPEFILE ||
        lp->connectiontype == MS_OGR ||
        lp->connectiontype == MS_TILED_SHAPEFILE ||
        lp->connectiontype == MS_INLINE)
      addtimebacktics = 1;
    else
      addtimebacktics = 0;

    
    /* parse the time string. We support dicrete times (eg 2004-09-21),  */
    /* multiple times (2004-09-21, 2004-09-22, ...) */
    /* and range(s) (2004-09-21/2004-09-25, 2004-09-27/2004-09-29) */

    if (strstr(timestring, ",") == NULL && 
        strstr(timestring, "/") == NULL) /* discrete time */
    {
        if(lp->filteritem) free(lp->filteritem);
        lp->filteritem = strdup(timefield);
        if (&lp->filter)
          freeExpression(&lp->filter);
        

        strcat(buffer, "(");
        if (addtimebacktics)
          strcat(buffer,  "`");

         if (addtimebacktics)
           strcat(buffer, "[");
        strcat(buffer, timefield);
        if (addtimebacktics)
          strcat(buffer, "]");
        if (addtimebacktics)
          strcat(buffer,  "`");

         
        strcat(buffer, " = ");
        if (addtimebacktics)
          strcat(buffer,  "`");
        else
          strcat(buffer,  "'");

        strcat(buffer, timestring);
        if (addtimebacktics)
          strcat(buffer,  "`");
        else
          strcat(buffer,  "'");

        strcat(buffer, ")");
        
        /* loadExpressionString(&lp->filter, (char *)timestring); */
        loadExpressionString(&lp->filter, buffer);

        return MS_TRUE;
    }
    
    atimes = split (timestring, ',', &numtimes);
    if (atimes == NULL || numtimes < 1)
      return MS_FALSE;

    if (numtimes >= 1)
    {
        /* check to see if we have ranges by parsing the first entry */
        tokens = split(atimes[0],  '/', &ntmp);
        if (ntmp == 2) /* ranges */
        {
                
            msFreeCharArray(tokens, ntmp);
             for (i=0; i<numtimes; i++)
             {
                 tokens = split(atimes[i],  '/', &ntmp);
                 if (ntmp == 2)
                 {
                     if (strlen(buffer) > 0)
                       strcat(buffer, " OR ");
                     else
                       strcat(buffer, "(");

                     strcat(buffer, "(");
                     if (addtimebacktics)
                       strcat(buffer,  "`");

                     if (addtimebacktics)
                       strcat(buffer, "[");
                     strcat(buffer, timefield);
                     if (addtimebacktics)
                       strcat(buffer, "]");
                     
                     if (addtimebacktics)
                       strcat(buffer,  "`");

                     strcat(buffer, " >= ");
                     if (addtimebacktics)
                       strcat(buffer,  "`");
                     else
                       strcat(buffer,  "'");

                     strcat(buffer, tokens[0]);
                     if (addtimebacktics)
                       strcat(buffer,  "`");
                     else
                       strcat(buffer,  "'");
                     strcat(buffer, " AND ");

                     if (addtimebacktics)
                       strcat(buffer,  "`");

                     if (addtimebacktics)
                       strcat(buffer, "[");
                     strcat(buffer, timefield);
                     if (addtimebacktics)
                       strcat(buffer, "]");
                     if (addtimebacktics)
                       strcat(buffer,  "`");

                     strcat(buffer, " <= ");
                     
                     if (addtimebacktics)
                       strcat(buffer,  "`");
                     else
                       strcat(buffer,  "'");
                     strcat(buffer, tokens[1]);
                     if (addtimebacktics)
                       strcat(buffer,  "`");
                     else
                       strcat(buffer,  "'");
                     strcat(buffer, ")");
                 }
                 
                  msFreeCharArray(tokens, ntmp);
             }
             if (strlen(buffer) > 0)
               strcat(buffer, ")");
        }
        else if (ntmp == 1) /* multiple times */
        {
             msFreeCharArray(tokens, ntmp);
             strcat(buffer, "(");
             for (i=0; i<numtimes; i++)
             {
                 if (i > 0)
                   strcat(buffer, " OR ");

                  strcat(buffer, "(");
                  if (addtimebacktics)
                    strcat(buffer, "`");
                  
                  if (addtimebacktics)
                    strcat(buffer, "[");
                  strcat(buffer, timefield);
                  if (addtimebacktics)
                    strcat(buffer, "]");

                  if (addtimebacktics)
                    strcat(buffer, "`");

                  strcat(buffer, " = ");
                  
                  if (addtimebacktics)
                    strcat(buffer, "`");
                  else
                    strcat(buffer,  "'");
                  strcat(buffer, atimes[i]);
                  if (addtimebacktics)
                    strcat(buffer,  "`");
                  else
                    strcat(buffer,  "'");
                  strcat(buffer, ")");
             } 
              strcat(buffer, ")");
        }
        else
        {
            msFreeCharArray(atimes, numtimes);
            return MS_FALSE;
        }

        msFreeCharArray(atimes, numtimes);

        /* load the string to the filter */
        if (strlen(buffer) > 0)
        {
            if(lp->filteritem) 
              free(lp->filteritem);
            lp->filteritem = strdup(timefield);     
            if (&lp->filter)
              freeExpression(&lp->filter);
            loadExpressionString(&lp->filter, buffer);
        }

        return MS_TRUE;
                 
    }
    
     return MS_FALSE;
}   
