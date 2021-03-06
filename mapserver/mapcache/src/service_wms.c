/*
 *  Copyright 2010 Thomas Bonfort
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "mapcache.h"
#include <apr_strings.h>
#include <math.h>
#include "ezxml.h"

/** \addtogroup services */
/** @{ */

void _create_capabilities_wms(mapcache_context *ctx, mapcache_request_get_capabilities *req, char *guessed_url, char *path_info, mapcache_cfg *cfg) {
   mapcache_request_get_capabilities_wms *request = (mapcache_request_get_capabilities_wms*)req;
#ifdef DEBUG
   if(request->request.request.type != MAPCACHE_REQUEST_GET_CAPABILITIES) {
      ctx->set_error(ctx,400,"wrong wms capabilities request");
      return;
   }
#endif
   ezxml_t caps, tmpxml;
   const char *title;
   const char *url = apr_table_get(cfg->metadata,"url");
   if(!url) {
      url = guessed_url;
   }
   
   url = apr_pstrcat(ctx->pool,url,req->request.service->url_prefix,"?",NULL);
   caps = ezxml_new("WMT_MS_Capabilities");
   ezxml_set_attr(caps,"version","1.1.1");
/*
          "<Service>\n"
            "<Name>OGC:WMS</Name>\n"
            "<Title>%s</Title>\n"
            "<OnlineResource xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:href=\"%s/wms?\"/>\n"
          "</Service>\n"
*/
   tmpxml = ezxml_add_child(caps,"Service",0);
   ezxml_set_txt(ezxml_add_child(tmpxml,"Name",0),"OGC:WMS");
   title = apr_table_get(cfg->metadata,"title");
   if(!title) {
      title = "no title set, add some in metadata";
   }
   ezxml_set_txt(ezxml_add_child(tmpxml,"Title",0),title);
   tmpxml = ezxml_add_child(tmpxml,"OnlineResource",0);
   ezxml_set_attr(tmpxml,"xmlns:xlink","http://www.w3.org/1999/xlink");
   ezxml_set_attr(tmpxml,"xlink:href",url);
   /*

      "<Capability>\n"
      "<Request>\n"
   */
   ezxml_t capxml = ezxml_add_child(caps,"Capability",0);
   ezxml_t reqxml = ezxml_add_child(capxml,"Request",0);
   /*
      "<GetCapabilities>\n"
      " <Format>application/vnd.ogc.wms_xml</Format>\n"
      " <DCPType>\n"
      "  <HTTP>\n"
      "   <Get><OnlineResource xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:href=\"%s/wms?\"/></Get>\n"
      "  </HTTP>\n"
      " </DCPType>\n"
      "</GetCapabilities>\n"
      */
   tmpxml = ezxml_add_child(reqxml,"GetCapabilities",0);
   ezxml_set_txt(ezxml_add_child(tmpxml,"Format",0),"application/vnd.ogc.wms_xml");
   tmpxml = ezxml_add_child(tmpxml,"DCPType",0);
   tmpxml = ezxml_add_child(tmpxml,"HTTP",0);
   tmpxml = ezxml_add_child(tmpxml,"Get",0);
   tmpxml = ezxml_add_child(tmpxml,"OnlineResource",0);
   ezxml_set_attr(tmpxml,"xmlns:xlink","http://www.w3.org/1999/xlink");
   ezxml_set_attr(tmpxml,"xlink:href",url);

/* 
              "<GetMap>\n"
                "<Format>image/png</Format>\n"
                "<Format>image/jpeg</Format>\n"
                "<DCPType>\n"
                  "<HTTP>\n"
                    "<Get><OnlineResource xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:href=\"%s/wms?\"/></Get>\n"
                  "</HTTP>\n"
                "</DCPType>\n"
              "</GetMap>\n"
*/
   tmpxml = ezxml_add_child(reqxml,"GetMap",0);
   ezxml_set_txt(ezxml_add_child(tmpxml,"Format",0),"image/png");
   ezxml_set_txt(ezxml_add_child(tmpxml,"Format",0),"image/jpeg");
   tmpxml = ezxml_add_child(tmpxml,"DCPType",0);
   tmpxml = ezxml_add_child(tmpxml,"HTTP",0);
   tmpxml = ezxml_add_child(tmpxml,"Get",0);
   tmpxml = ezxml_add_child(tmpxml,"OnlineResource",0);
   ezxml_set_attr(tmpxml,"xmlns:xlink","http://www.w3.org/1999/xlink");
   ezxml_set_attr(tmpxml,"xlink:href",url);

   
/*
              "<GetFeatureInfo>\n"
                "<Format>text/plain</Format>\n"
                "<Format>application/vnd.ogc.gml</Format>\n"
                "<DCPType>\n"
                  "<HTTP>\n"
                    "<Get>\n"
                      "<OnlineResource xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:type=\"simple\" xlink:href=\"%s/wms?\" />\n"
                    "</Get>\n"
                  "</HTTP>\n"
                "</DCPType>\n"
              "</GetFeatureInfo>\n"
*/
   tmpxml = ezxml_add_child(reqxml,"GetFeatureInfo",0);
   ezxml_set_txt(ezxml_add_child(tmpxml,"Format",0),"text/plain");
   ezxml_set_txt(ezxml_add_child(tmpxml,"Format",0),"application/vnd.ogc.gml");
   tmpxml = ezxml_add_child(tmpxml,"DCPType",0);
   tmpxml = ezxml_add_child(tmpxml,"HTTP",0);
   tmpxml = ezxml_add_child(tmpxml,"Get",0);
   tmpxml = ezxml_add_child(tmpxml,"OnlineResource",0);
   ezxml_set_attr(tmpxml,"xmlns:xlink","http://www.w3.org/1999/xlink");
   ezxml_set_attr(tmpxml,"xlink:href",url);

/*
            "<Exception>\n"
              "<Format>text/plain</Format>\n"
            "</Exception>\n"
*/
      
   tmpxml = ezxml_add_child(capxml,"Exceptions",0);
   ezxml_set_txt(ezxml_add_child(tmpxml,"Format",0),"text/plain");

   ezxml_t vendorxml = ezxml_add_child(capxml,"VendorSpecificCapabilities",0);
   ezxml_t toplayer = ezxml_add_child(capxml,"Layer",0);
   tmpxml = ezxml_add_child(toplayer,"Name",0);
   ezxml_set_txt(tmpxml,"rootlayer");
   tmpxml = ezxml_add_child(toplayer,"Title",0);
   ezxml_set_txt(tmpxml,title);

   /* 
    * announce all layer srs's in the root layer. This part of the wms spec we
    * cannot respect with a caching solution, as each tileset can only be served
    * under a specified number of projections.
    *
    * TODO: check for duplicates in gris srs
    */
   apr_hash_index_t *grid_index = apr_hash_first(ctx->pool,cfg->grids);
   while(grid_index) {
      const void *key;
      apr_ssize_t keylen;
      mapcache_grid *grid = NULL;
      apr_hash_this(grid_index,&key,&keylen,(void**)&grid);
      ezxml_set_txt(ezxml_add_child(toplayer,"SRS",0),grid->srs);
      grid_index = apr_hash_next(grid_index);
   }

   
   apr_hash_index_t *tileindex_index = apr_hash_first(ctx->pool,cfg->tilesets);

   while(tileindex_index) {
      mapcache_tileset *tileset;
      const void *key; apr_ssize_t keylen;
      apr_hash_this(tileindex_index,&key,&keylen,(void**)&tileset);
      
      ezxml_t layerxml = ezxml_add_child(toplayer,"Layer",0);
      ezxml_set_attr(layerxml, "cascaded", "1");
      ezxml_set_attr(layerxml, "queryable", (tileset->source && tileset->source->info_formats)?"1":"0"),
      ezxml_set_txt(ezxml_add_child(layerxml,"Name",0),tileset->name);

      ezxml_t tsxml = ezxml_add_child(vendorxml, "TileSet",0);
      
      /*optional layer title*/
      const char *title = apr_table_get(tileset->metadata,"title");
      if(title) {
         ezxml_set_txt(ezxml_add_child(layerxml,"Title",0),title);
      }

      /*optional layer abstract*/
      const char *abstract = apr_table_get(tileset->metadata,"abstract");
      if(abstract) {
         ezxml_set_txt(ezxml_add_child(layerxml,"Abstract",0),abstract);
      }

      if(tileset->wgs84bbox[0] != tileset->wgs84bbox[2]) {
         ezxml_t wgsxml = ezxml_add_child(layerxml,"LatLonBoundingBox",0);
         ezxml_set_attr(wgsxml,"minx",apr_psprintf(ctx->pool,"%f",tileset->wgs84bbox[0]));
         ezxml_set_attr(wgsxml,"miny",apr_psprintf(ctx->pool,"%f",tileset->wgs84bbox[1]));
         ezxml_set_attr(wgsxml,"maxx",apr_psprintf(ctx->pool,"%f",tileset->wgs84bbox[2]));
         ezxml_set_attr(wgsxml,"maxy",apr_psprintf(ctx->pool,"%f",tileset->wgs84bbox[3]));
      }

      if(tileset->dimensions) {
         int i;
         for(i=0;i<tileset->dimensions->nelts;i++) {
            mapcache_dimension *dimension = APR_ARRAY_IDX(tileset->dimensions,i,mapcache_dimension*);
            ezxml_t dimxml = ezxml_add_child(layerxml,"Dimension",0);
            ezxml_set_attr(dimxml,"name",dimension->name);
            ezxml_set_attr(dimxml,"default",dimension->default_value);

            if(dimension->unit) {
               ezxml_set_attr(dimxml,"units",dimension->unit);
            }
            const char **value = dimension->print_ogc_formatted_values(ctx,dimension);
            char *dimval = apr_pstrdup(ctx->pool,*value);
            value++;
            while(*value) {
               dimval = apr_pstrcat(ctx->pool,dimval,",",*value,NULL);
               value++;
            }
            ezxml_set_txt(dimxml,dimval);
         }
      }


      int i;
      for(i=0;i<tileset->grid_links->nelts;i++) {
         mapcache_grid_link *gridlink = APR_ARRAY_IDX(tileset->grid_links,i,mapcache_grid_link*);
         mapcache_grid *grid = gridlink->grid;
         double *extent = grid->extent;
         if(gridlink->restricted_extent)
            extent = gridlink->restricted_extent;
         ezxml_t bboxxml = ezxml_add_child(layerxml,"BoundingBox",0);
         ezxml_set_attr(bboxxml,"SRS", grid->srs);
         ezxml_set_attr(bboxxml,"minx",apr_psprintf(ctx->pool,"%f",extent[0]));
         ezxml_set_attr(bboxxml,"miny",apr_psprintf(ctx->pool,"%f",extent[1]));
         ezxml_set_attr(bboxxml,"maxx",apr_psprintf(ctx->pool,"%f",extent[2]));
         ezxml_set_attr(bboxxml,"maxy",apr_psprintf(ctx->pool,"%f",extent[3]));
         ezxml_set_txt(ezxml_add_child(layerxml,"SRS",0),grid->srs);
         int j;
         for(j=0;j<gridlink->grid->srs_aliases->nelts;j++) {
            ezxml_set_txt(ezxml_add_child(layerxml,"SRS",0),APR_ARRAY_IDX(gridlink->grid->srs_aliases,j,char*));
         }


         if(i==0) {
            /*wms-c only supports one grid per layer, so we use the first of the tileset's grids */
            ezxml_set_txt(ezxml_add_child(tsxml,"SRS",0),grid->srs);
            tmpxml = ezxml_add_child(tsxml,"BoundingBox",0);
            ezxml_set_attr(tmpxml,"SRS",grid->srs);
            ezxml_set_attr(tmpxml,"minx",apr_psprintf(ctx->pool,"%f",grid->extent[0]));
            ezxml_set_attr(tmpxml,"miny",apr_psprintf(ctx->pool,"%f",grid->extent[1]));
            ezxml_set_attr(tmpxml,"maxx",apr_psprintf(ctx->pool,"%f",grid->extent[2]));
            ezxml_set_attr(tmpxml,"maxy",apr_psprintf(ctx->pool,"%f",grid->extent[3]));

            char *resolutions="";

            int i;
            for(i=0;i<grid->nlevels;i++) {
               resolutions = apr_psprintf(ctx->pool,"%s%.20f ",resolutions,grid->levels[i]->resolution);
            }
            ezxml_set_txt(ezxml_add_child(tsxml,"Resolutions",0),resolutions);
            ezxml_set_txt(ezxml_add_child(tsxml,"Width",0),apr_psprintf(ctx->pool,"%d",grid->tile_sx));
            ezxml_set_txt(ezxml_add_child(tsxml,"Height",0),apr_psprintf(ctx->pool,"%d", grid->tile_sy));
         }
      }
      if(tileset->format && tileset->format->mime_type) {
         ezxml_set_txt(ezxml_add_child(tsxml,"Format",0),tileset->format->mime_type);
      } else {
         ezxml_set_txt(ezxml_add_child(tsxml,"Format",0),"image/unknown");
      }
      ezxml_set_txt(ezxml_add_child(tsxml,"Layers",0),tileset->name);
      ezxml_set_txt(ezxml_add_child(tsxml,"Styles",0),"");
      tileindex_index = apr_hash_next(tileindex_index);
   }

   
   char *tmpcaps = ezxml_toxml(caps);
   ezxml_free(caps);
   static char *capheader=
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\
<!DOCTYPE WMT_MS_Capabilities SYSTEM \"http://schemas.opengis.net/wms/1.1.0/capabilities_1_1_0.dtd\"\
[\
 <!ELEMENT VendorSpecificCapabilities EMPTY>\
]>\n";
   request->request.capabilities = apr_pstrcat(ctx->pool,capheader,tmpcaps,NULL);
   free(tmpcaps);
   request->request.mime_type = apr_pstrdup(ctx->pool,"text/xml");
}

/**
 * \brief parse a WMS request
 * \private \memberof mapcache_service_wms
 * \sa mapcache_service::parse_request()
 */
void _mapcache_service_wms_parse_request(mapcache_context *ctx, mapcache_service *this, mapcache_request **request,
      const char *pathinfo, apr_table_t *params, mapcache_cfg *config) {
   const char *str = NULL;
   const char *srs=NULL;
   int width=0, height=0;
   double *bbox;
   int isGetMap=0,iswms130=0;
   int errcode = 200;
   char *errmsg = NULL;
   mapcache_service_wms *wms_service = (mapcache_service_wms*)this;

   *request = NULL;

   str = apr_table_get(params,"SERVICE");
   if(!str) {
      errcode=400;
      errmsg = "received wms request with no service param";
      goto proxies;
   }
   if( strcasecmp(str,"wms") ) {
      errcode = 400;
      errmsg = apr_psprintf(ctx->pool,"received wms request with invalid service param %s", str);
      goto proxies;
   }
      
   str = apr_table_get(params,"REQUEST");
   if(!str) {
      errcode = 400;
      errmsg = "received wms with no request";
      goto proxies;
   }

   if( ! strcasecmp(str,"getmap")) {
      isGetMap = 1;
      str = apr_table_get(params,"VERSION");
      if(!strcmp(str,"1.3.0")) {
         iswms130 = 1;
      }
   } else {
      if( ! strcasecmp(str,"getcapabilities") ) {
         *request = (mapcache_request*)
            apr_pcalloc(ctx->pool,sizeof(mapcache_request_get_capabilities_wms));
         (*request)->type = MAPCACHE_REQUEST_GET_CAPABILITIES;
         goto proxies; /* OK */
      } else if( ! strcasecmp(str,"getfeatureinfo") ) {
         //nothing
      } else {
         errcode = 501;
         errmsg = apr_psprintf(ctx->pool,"received wms with invalid request %s",str);
         goto proxies;
      }
   }


   str = apr_table_get(params,"BBOX");
   if(!str) {
      errcode = 400;
      errmsg = "received wms request with no bbox";
      goto proxies;
   } else {
      int nextents;
      if(MAPCACHE_SUCCESS != mapcache_util_extract_double_list(ctx, str,",",&bbox,&nextents) ||
            nextents != 4) {
         errcode = 400;
         errmsg = "received wms request with invalid bbox";
         goto proxies;
      }
   }

   str = apr_table_get(params,"WIDTH");
   if(!str) {
      errcode = 400;
      errmsg = "received wms request with no width";
      goto proxies;
   } else {
      char *endptr;
      width = (int)strtol(str,&endptr,10);
      if(*endptr != 0 || width <= 0) {
         errcode = 400;
         errmsg = "received wms request with invalid width";
         goto proxies;
      }
   }

   str = apr_table_get(params,"HEIGHT");
   if(!str) {
      errcode = 400;
      errmsg = "received wms request with no height";
      goto proxies;
   } else {
      char *endptr;
      height = (int)strtol(str,&endptr,10);
      if(*endptr != 0 || height <= 0) {
         errcode = 400;
         errmsg = "received wms request with invalid height";
         goto proxies;
      }
   }

   if(iswms130) {
      srs = apr_table_get(params,"CRS");
      if(!srs) {
         errcode = 400;
         errmsg = "received wms request with no crs";
         goto proxies;
      }
   } else {
      srs = apr_table_get(params,"SRS");
      if(!srs) {
         errcode = 400;
         errmsg = "received wms request with no srs";
         goto proxies;
      }
   }
   if(iswms130) {
      /*check if we should flib the axis order*/
      if(mapcache_is_axis_inverted(srs)) {
         double swap;
         swap = bbox[0];
         bbox[0] = bbox[1];
         bbox[1] = swap;
         swap = bbox[2];
         bbox[2] = bbox[3];
         bbox[3] = swap;
      }
   }

   if(isGetMap) {
      str = apr_table_get(params,"LAYERS");
      if(!str) {
         errcode = 400;
         errmsg = "received wms request with no layers";
         goto proxies;
      } else {
         char *last, *layers;
         const char *key;
         int count=1;
         int i,layeridx;
         int x,y,z;
         mapcache_request_get_map *map_req = NULL;
         mapcache_request_get_tile *tile_req = NULL;
         mapcache_grid_link *main_grid_link = NULL;
         mapcache_tileset *main_tileset = NULL;
         
         /* count the number of layers that are requested */
         for(key=str;*key;key++) if(*key == ',') count++;

         /* 
          * look to see if we have a getTile or a getMap request. We do this by looking at the first
          * wms layer that was provided in the request.
          * Checking to see if all requested layers reference the grid will be done in a second step
          */
         mapcache_request_type type = MAPCACHE_REQUEST_GET_TILE;
         
         if(count ==1) {
            key = str;
         } else {
            layers = apr_pstrdup(ctx->pool,str);
            key = apr_strtok(layers, ",", &last); /* extract first layer */
         }
         main_tileset = mapcache_configuration_get_tileset(config,key);
         if(!main_tileset) {
            errcode = 404;
            errmsg = apr_psprintf(ctx->pool,"received wms request with invalid layer %s", key);
            goto proxies;
         }

         for(i=0;i<main_tileset->grid_links->nelts;i++){
            mapcache_grid_link *sgrid = APR_ARRAY_IDX(main_tileset->grid_links,i,mapcache_grid_link*);
            /* look for a grid with a matching srs */
            if(strcasecmp(sgrid->grid->srs,srs)) {
               /* look if the grid has some srs aliases */
               int s;
               for(s=0;s<sgrid->grid->srs_aliases->nelts;s++) {
                  char *srsalias = APR_ARRAY_IDX(sgrid->grid->srs_aliases,s,char*);
                  if(!strcasecmp(srsalias,srs)) break;
               }
               if(s==sgrid->grid->srs_aliases->nelts)
                  continue; /* no srs alias matches the requested srs */
            }
            main_grid_link = sgrid;
            break;
         }
         if(!main_grid_link) {
            errcode = 400;
            errmsg = apr_psprintf(ctx->pool,
                  "received unsuitable wms request: no <grid> with suitable srs found for layer %s",main_tileset->name);
            goto proxies;
         }

         /* verify we align on the tileset's grid */ 
         if(main_grid_link->grid->tile_sx != width || main_grid_link->grid->tile_sy != height ||
               mapcache_grid_get_cell(ctx, main_grid_link->grid, bbox, &x,&y,&z) != MAPCACHE_SUCCESS) {
            /* we have the correct srs, but the request does not align on the grid */
            type = MAPCACHE_REQUEST_GET_MAP;
         }
         

         if(type == MAPCACHE_REQUEST_GET_TILE) {
            tile_req = apr_pcalloc(ctx->pool, sizeof(mapcache_request_get_tile));
            tile_req->request.type = MAPCACHE_REQUEST_GET_TILE;
            tile_req->tiles = apr_pcalloc(ctx->pool, count*sizeof(mapcache_tile*));
            tile_req->format = wms_service->getmap_format;
            *request = (mapcache_request*)tile_req;
         } else {
            map_req = apr_pcalloc(ctx->pool, sizeof(mapcache_request_get_map));
            map_req->request.type = MAPCACHE_REQUEST_GET_MAP;
            map_req->maps = apr_pcalloc(ctx->pool, count*sizeof(mapcache_map*));
            map_req->getmap_strategy = wms_service->getmap_strategy;
            map_req->resample_mode = wms_service->resample_mode;
            map_req->getmap_format = wms_service->getmap_format;
            *request = (mapcache_request*)map_req;
         }

         /*
          * loop through all the layers to verify that they reference the requested grid,
          * and to extract any dimensions if configured
          */
         if(count>1)
            layers = apr_pstrdup(ctx->pool,str); /* apr_strtok modifies its input string */

         for (layeridx=0,key = ((count==1)?str:apr_strtok(layers, ",", &last)); key != NULL;
               key = ((count==1)?NULL:apr_strtok(NULL, ",", &last)),layeridx++) {
            mapcache_tileset *tileset = main_tileset;
            mapcache_grid_link *grid_link = main_grid_link;
            apr_table_t *dimtable = NULL;
            
            if(layeridx) {
               /* 
                * if we have multiple requested layers, check that they reference the requested grid
                * this step is not done for the first tileset as we have already performed it
                */
               tileset = mapcache_configuration_get_tileset(config,key);
               int i;
               grid_link = NULL;
               for(i=0;i<tileset->grid_links->nelts;i++){
                  grid_link = APR_ARRAY_IDX(tileset->grid_links,i,mapcache_grid_link*);
                  if(grid_link->grid == main_grid_link->grid) {
                     break;
                  }
               }
               if(i==tileset->grid_links->nelts) {
                  /* the tileset does not reference the grid of the first tileset */
                  errcode = 400;
                  errmsg = apr_psprintf(ctx->pool,
                        "tileset %s does not reference grid %s (referenced by tileset %s)",
                        tileset->name, main_grid_link->grid->name,main_tileset->name);
                  goto proxies;
               }
            }
            if(type == MAPCACHE_REQUEST_GET_TILE) {
               mapcache_tile *tile = mapcache_tileset_tile_create(ctx->pool, tileset, grid_link);
               tile->x = x;
               tile->y = y;
               tile->z = z;
               mapcache_tileset_tile_validate(ctx,tile);
               if(GC_HAS_ERROR(ctx)) {
                  /* don't bail out just yet, in case multiple tiles have been requested */
                  ctx->clear_errors(ctx);
               } else {
                  tile_req->tiles[tile_req->ntiles++] = tile;
               }
               dimtable = tile->dimensions;

            } else {
               mapcache_map *map = mapcache_tileset_map_create(ctx->pool,tileset,grid_link);
               map->width = width;
               map->height = height;
               map->extent[0] = bbox[0];
               map->extent[1] = bbox[1];
               map->extent[2] = bbox[2];
               map->extent[3] = bbox[3];
               map_req->maps[map_req->nmaps++] = map;
               dimtable = map->dimensions;
            }

            /*look for dimensions*/
            if(dimtable) {
               int i;
               for(i=0;i<tileset->dimensions->nelts;i++) {
                  mapcache_dimension *dimension = APR_ARRAY_IDX(tileset->dimensions,i,mapcache_dimension*);
                  const char *value;
                  if((value = (char*)apr_table_get(params,dimension->name)) != NULL) {
                     char *tmpval = apr_pstrdup(ctx->pool,value);
                     int ok = dimension->validate(ctx,dimension,&tmpval);
                     GC_CHECK_ERROR(ctx);
                     if(ok == MAPCACHE_SUCCESS)
                        apr_table_setn(dimtable,dimension->name,tmpval);
                     else {
                        errcode = 400;
                        errmsg = apr_psprintf(ctx->pool, "dimension \"%s\" value \"%s\" fails to validate",
                              dimension->name, value);
                        goto proxies;
                     }
                  }
               }
            }
         }
         if(tile_req && tile_req->ntiles == 0) {
            errcode = 404;
            errmsg = "request for tile outside of restricted extent";
            goto proxies;
         }
      }
   } else {
      //getfeatureinfo
      str = apr_table_get(params,"QUERY_LAYERS");
      if(!str) {
         errcode = 400;
         errmsg = "received wms getfeatureinfo request with no query layers";
         goto proxies;
      } else if(strstr(str,",")) {
         errcode = 501;
         errmsg = "wms getfeatureinfo not implemented for multiple layers";
         goto proxies;
      } else {
         mapcache_tileset *tileset = mapcache_configuration_get_tileset(config,str);
         if(!tileset) {
            errcode = 404;
            errmsg = apr_psprintf(ctx->pool,"received wms getfeatureinfo request with invalid layer %s", str);
            goto proxies;
         }
         if(!tileset->source || !tileset->source->info_formats) {
            errcode = 404;
            errmsg = apr_psprintf(ctx->pool,"received wms getfeatureinfo request for unqueryable layer %s", str);
            goto proxies;
         }
         int i;
         mapcache_grid_link *grid_link = NULL;
         for(i=0;i<tileset->grid_links->nelts;i++){
            mapcache_grid_link *sgrid = APR_ARRAY_IDX(tileset->grid_links,i,mapcache_grid_link*);
            if(strcasecmp(sgrid->grid->srs,srs)) continue;
            grid_link = sgrid;
            break;
         }
         if(!grid_link) {
            errcode = 400;
            errmsg = apr_psprintf(ctx->pool,
                  "received unsuitable wms request: no <grid> with suitable srs found for layer %s",tileset->name);
            goto proxies;
         }
         int x,y;
         
         str = apr_table_get(params,"X");
         if(!str) {
            errcode = 400;
            errmsg = "received wms getfeatureinfo request with no X";
            goto proxies;
         } else {
            char *endptr;
            x = (int)strtol(str,&endptr,10);
            if(*endptr != 0 || x <= 0 || x>=width) {
               errcode = 400;
               errmsg = "received wms request with invalid X";
               goto proxies;
            }
         }

         str = apr_table_get(params,"Y");
         if(!str) {
            errcode = 400;
            errmsg = "received wms getfeatureinfo request with no Y";
            goto proxies;
         } else {
            char *endptr;
            y = (int)strtol(str,&endptr,10);
            if(*endptr != 0 || y <= 0 || y>=height) {
               errcode = 400;
               errmsg = "received wms request with invalid Y";
               goto proxies;
            }
         }
         
         mapcache_feature_info *fi = mapcache_tileset_feature_info_create(ctx->pool, tileset, grid_link);
         fi->i = x;
         fi->j = y;
         fi->format = apr_pstrdup(ctx->pool,apr_table_get(params,"INFO_FORMAT"));
         if(!fi->format) {
            errcode = 400;
            errmsg = "received wms getfeatureinfo request with no INFO_FORMAT";
            goto proxies;
         }

         if(fi->map.dimensions) {
            int i;
            for(i=0;i<tileset->dimensions->nelts;i++) {
               mapcache_dimension *dimension = APR_ARRAY_IDX(tileset->dimensions,i,mapcache_dimension*);
               const char *value;
               if((value = (char*)apr_table_get(params,dimension->name)) != NULL) {
                  char *tmpval = apr_pstrdup(ctx->pool,value);
                  int ok = dimension->validate(ctx,dimension,&tmpval);
                  GC_CHECK_ERROR(ctx);
                  if(ok == MAPCACHE_SUCCESS)
                     apr_table_setn(fi->map.dimensions,dimension->name,tmpval);
                  else {
                     errcode = 400;
                     errmsg = apr_psprintf(ctx->pool,"dimension \"%s\" value \"%s\" fails to validate",
                           dimension->name, value);
                     goto proxies;
                  }
               }
            }
         }
         fi->map.width = width;
         fi->map.height = height;
         fi->map.extent[0] = bbox[0];
         fi->map.extent[1] = bbox[1];
         fi->map.extent[2] = bbox[2];
         fi->map.extent[3] = bbox[3];
         mapcache_request_get_feature_info *req_fi = apr_pcalloc(ctx->pool, sizeof(mapcache_request_get_feature_info));
         req_fi->request.type = MAPCACHE_REQUEST_GET_FEATUREINFO;
         req_fi->fi = fi;
         *request = (mapcache_request*)req_fi;

      }
   }

proxies:
   /* 
    * if we don't have a gettile or getmap request we can treat from the cache tiles, look to see if we have a rule
    * that tells us to forward the request somewhere else
    */
   if(errcode == 200 && 
         *request && (
            /* if its a single tile we're ok*/
            ((*request)->type == MAPCACHE_REQUEST_GET_TILE && ((mapcache_request_get_tile*)(*request))->ntiles == 1) ||
            ((*request)->type == MAPCACHE_REQUEST_GET_FEATUREINFO) ||
            
            /* if we have a getmap or multiple tiles, we must check that assembling is allowed */
            (((*request)->type == MAPCACHE_REQUEST_GET_MAP || ( 
               (*request)->type == MAPCACHE_REQUEST_GET_TILE && ((mapcache_request_get_tile*)(*request))->ntiles > 1)) && 
               wms_service->getmap_strategy == MAPCACHE_GETMAP_ASSEMBLE)
            )) {
      /* if we're here, then we have succesfully parsed the request and can treat it ourselves, i.e. from cached tiles */
      return;
   } else {
      /* look to see if we can proxy the request somewhere*/
      int i,j;
      for(i=0;i<wms_service->forwarding_rules->nelts;i++) {
         mapcache_forwarding_rule *rule = APR_ARRAY_IDX(wms_service->forwarding_rules,i,mapcache_forwarding_rule*);
         int got_a_match = 1;
         for(j=0;j<rule->match_params->nelts;j++) {
            mapcache_dimension *match_param = APR_ARRAY_IDX(rule->match_params,j,mapcache_dimension*);
            const char *value = apr_table_get(params,match_param->name);
            if(!value || match_param->validate(ctx,match_param,(char**)&value) == MAPCACHE_FAILURE) {
               /* the parameter was not supplied, or did not validate: we don't apply this rule */
               got_a_match = 0;
               break;
            }
         }
         if( got_a_match == 1 ) {
            mapcache_request_proxy *req_proxy = apr_pcalloc(ctx->pool,sizeof(mapcache_request_proxy));
            *request = (mapcache_request*)req_proxy;
            (*request)->service = this;
            (*request)->type = MAPCACHE_REQUEST_PROXY;
            req_proxy->http = rule->http;
            req_proxy->params = params;
            if(rule->append_pathinfo) {
               req_proxy->pathinfo = pathinfo;
            } else {
               req_proxy->pathinfo = NULL;
            }
            return;
         }
      }
   }

   /* 
    * if we are here, then we are either in the getfeatureinfo / getcapabilities / getmap case,
    * or there was an error parsing the request and no rules to proxy it elsewhere
    */
   if(errcode != 200) {
      ctx->set_error(ctx,errcode,errmsg);
      return;
   }
#ifdef DEBUG
   if((*request)->type != MAPCACHE_REQUEST_GET_TILE &&
         (*request)->type != MAPCACHE_REQUEST_GET_MAP && 
         (*request)->type != MAPCACHE_REQUEST_GET_FEATUREINFO && 
         (*request)->type != MAPCACHE_REQUEST_GET_CAPABILITIES) {
      ctx->set_error(ctx,500,"BUG: request not gettile or getmap");
      return;
   }
#endif
}

#ifdef ENABLE_UNMAINTAINED_JSON_PARSER
void _configuration_parse_wms_json(mapcache_context *ctx, cJSON *node, mapcache_service *gservice, mapcache_cfg *cfg) {
   assert(gservice->type == MAPCACHE_SERVICE_WMS);
   mapcache_service_wms *wms = (mapcache_service_wms*)gservice;
   cJSON *rules = cJSON_GetObjectItem(node,"forwarding_rules");
   if(rules) {
      int i;
      for(i=0;i<cJSON_GetArraySize(rules);i++) {
         mapcache_forwarding_rule *rule = apr_pcalloc(ctx->pool, sizeof(mapcache_forwarding_rule));
         rule->match_params = apr_array_make(ctx->pool,1,sizeof(mapcache_dimension*));
         cJSON *jrule = cJSON_GetArrayItem(rules,i);
         
         /* extract name */
         cJSON *tmp  = cJSON_GetObjectItem(jrule,"name");
         if(tmp && tmp->valuestring) {
            rule->name = apr_pstrdup(ctx->pool,tmp->valuestring);
         } else {
            rule->name = apr_pstrdup(ctx->pool,"(null)");
         }
         
         /* should the remaining pathinfo be appended to the proxied request ? */
         tmp  = cJSON_GetObjectItem(jrule,"append_pathinfo");
         if(tmp && tmp->type == cJSON_True) {
            rule->append_pathinfo = 1;
         } else {
            rule->append_pathinfo = 0;
         }

         tmp = cJSON_GetObjectItem(jrule,"http");
         if(!tmp || tmp->type != cJSON_Object) {
            ctx->set_error(ctx,500,"rule \"%s\" does not contain an http object",rule->name);
            return;
         }
         rule->http = mapcache_http_configuration_parse_json(ctx,tmp);
         GC_CHECK_ERROR(ctx);
         
         tmp = cJSON_GetObjectItem(jrule,"params");
         if(tmp && cJSON_GetArraySize(tmp)) {
            int j;
            for(j=0;j<cJSON_GetArraySize(tmp);j++) {
               cJSON *param = cJSON_GetArrayItem(tmp,j);
               mapcache_dimension *dimension = NULL;
               char *name=NULL,*type=NULL;
               cJSON *tmp2 = cJSON_GetObjectItem(param,"name");
               if(tmp2 && tmp2->valuestring) {
                  name = tmp2->valuestring;
               }
               tmp2 = cJSON_GetObjectItem(param,"type");
               if(tmp2 && tmp2->valuestring) {
                  type = tmp2->valuestring;
               }
               if(!name || !type) {
                  ctx->set_error(ctx, 400, "mandatory \"name\" or \"type\" not found in forwarding rule params");
                  return;
               }
               if(!strcmp(type,"values")) {
                  dimension = mapcache_dimension_values_create(ctx->pool);
               } else if(!strcmp(type,"regex")) {
                  dimension = mapcache_dimension_regex_create(ctx->pool);
               } else {
                  ctx->set_error(ctx,400,"unknown forwarding_rule param type \"%s\". expecting \"values\" or \"regex\".",type);
                  return;
               }
               dimension->name = apr_pstrdup(ctx->pool,name);
               tmp2 = cJSON_GetObjectItem(param,"props");
               if(!tmp2 || tmp2->type != cJSON_Object) {
                  ctx->set_error(ctx, 400, "forwarding_rule \"%s\" param \"%s\" has no props",rule->name,name);
               }

               dimension->configuration_parse_json(ctx,dimension,tmp2);
               GC_CHECK_ERROR(ctx);

               APR_ARRAY_PUSH(rule->match_params,mapcache_dimension*) = dimension;
            }
            APR_ARRAY_PUSH(wms->forwarding_rules,mapcache_forwarding_rule*) = rule;
         }
      }
   }
   cJSON *full_getmap = cJSON_GetObjectItem(node,"full_getmap");
   if(full_getmap && full_getmap->type == cJSON_Object) {
      cJSON *tmp;
      tmp = cJSON_GetObjectItem(full_getmap,"strategy");
      if(tmp && tmp->valuestring) {
         if(!strcasecmp("assemble",tmp->valuestring)) {
            wms->getmap_strategy = MAPCACHE_GETMAP_ASSEMBLE;
         } else if(!strcasecmp("forward",tmp->valuestring)) {
            wms->getmap_strategy = MAPCACHE_GETMAP_FORWARD;
         } else if(!strcasecmp("error",tmp->valuestring)) {
            wms->getmap_strategy = MAPCACHE_GETMAP_ERROR;
         } else {
            ctx->set_error(ctx,400,"unknown full_getmap strategy \"%s\"",tmp->valuestring);
            return;
         }
      }
      tmp = cJSON_GetObjectItem(full_getmap,"resample_mode");
      if(tmp && tmp->valuestring) {
         if(!strcasecmp("bilinear",tmp->valuestring)) {
            wms->resample_mode = MAPCACHE_RESAMPLE_BILINEAR;
         } else if(!strcasecmp("nearest",tmp->valuestring)) {
            wms->resample_mode = MAPCACHE_RESAMPLE_NEAREST;
         } else {
            ctx->set_error(ctx,400,"unknown full_getmap resample_mode \"%s\"",tmp->valuestring);
            return;
         }
      }
      tmp = cJSON_GetObjectItem(full_getmap,"format");
      if(tmp && tmp->valuestring) {
         wms->getmap_format = mapcache_configuration_get_image_format(cfg,tmp->valuestring);
         if(!wms->getmap_format) {
            ctx->set_error(ctx,400,"unknown full_getmap format \"%s\"",tmp->valuestring);
            return;
         }
      }
   }
}
#endif

void _configuration_parse_wms_xml(mapcache_context *ctx, ezxml_t node, mapcache_service *gservice, mapcache_cfg *cfg) {
   assert(gservice->type == MAPCACHE_SERVICE_WMS);
   mapcache_service_wms *wms = (mapcache_service_wms*)gservice;
   ezxml_t rule_node;
   for( rule_node = ezxml_child(node,"forwarding_rule"); rule_node; rule_node = rule_node->next) {
      char *name = (char*)ezxml_attr(rule_node,"name");
      if(!name) name = "(null)";
      mapcache_forwarding_rule *rule = apr_pcalloc(ctx->pool, sizeof(mapcache_forwarding_rule));
      rule->name = apr_pstrdup(ctx->pool,name);
      rule->match_params = apr_array_make(ctx->pool,1,sizeof(mapcache_dimension*));

      ezxml_t pathinfo_node = ezxml_child(rule_node,"append_pathinfo");
      if(pathinfo_node && !strcasecmp(pathinfo_node->txt,"true")) {
         rule->append_pathinfo = 1;
      } else {
         rule->append_pathinfo = 0;
      }
      ezxml_t http_node = ezxml_child(rule_node,"http");
      if(!http_node) {
         ctx->set_error(ctx,500,"rule \"%s\" does not contain an <http> block",name);
         return;
      }
      rule->http = mapcache_http_configuration_parse_xml(ctx,http_node);
      GC_CHECK_ERROR(ctx);

      ezxml_t param_node;
      for(param_node = ezxml_child(rule_node,"param"); param_node; param_node = param_node->next) {
         char *name = (char*)ezxml_attr(param_node,"name");
         char *type = (char*)ezxml_attr(param_node,"type");

         mapcache_dimension *dimension = NULL;

         if(!name || !strlen(name)) {
            ctx->set_error(ctx, 400, "mandatory attribute \"name\" not found in forwarding rule <param>");
            return;
         }

         if(type && *type) {
            if(!strcmp(type,"values")) {
               dimension = mapcache_dimension_values_create(ctx->pool);
            } else if(!strcmp(type,"regex")) {
               dimension = mapcache_dimension_regex_create(ctx->pool);
            } else {
               ctx->set_error(ctx,400,"unknown <param> type \"%s\". expecting \"values\" or \"regex\".",type);
               return;
            }
         } else {
            ctx->set_error(ctx,400, "mandatory attribute \"type\" not found in <dimensions>");
            return;
         }

         dimension->name = apr_pstrdup(ctx->pool,name);

         dimension->configuration_parse_xml(ctx,dimension,param_node);
         GC_CHECK_ERROR(ctx);

         APR_ARRAY_PUSH(rule->match_params,mapcache_dimension*) = dimension;
      }
      APR_ARRAY_PUSH(wms->forwarding_rules,mapcache_forwarding_rule*) = rule;
   }
   if ((rule_node = ezxml_child(node,"full_wms")) != NULL) {
      if(!strcmp(rule_node->txt,"assemble")) {
         wms->getmap_strategy = MAPCACHE_GETMAP_ASSEMBLE;
      } else if(!strcmp(rule_node->txt,"forward")) {
         wms->getmap_strategy = MAPCACHE_GETMAP_FORWARD;
      } else if(*rule_node->txt && strcmp(rule_node->txt,"error")) {
         ctx->set_error(ctx,400, "unknown value %s for node <full_wms> (allowed values: assemble, getmap or error", rule_node->txt);
         return;
      }
   }
   
   wms->getmap_format = mapcache_configuration_get_image_format(cfg,"JPEG");
   if ((rule_node = ezxml_child(node,"format")) != NULL) {
      wms->getmap_format = mapcache_configuration_get_image_format(cfg,rule_node->txt);
      if(!wms->getmap_format) {
         ctx->set_error(ctx,400, "unknown <format> %s for wms service", rule_node->txt);
         return;
      }
   }
   
   if ((rule_node = ezxml_child(node,"resample_mode")) != NULL) {
      if(!strcmp(rule_node->txt,"nearest")) {
         wms->resample_mode = MAPCACHE_RESAMPLE_NEAREST;
      } else if(!strcmp(rule_node->txt,"bilinear")) {
         wms->resample_mode = MAPCACHE_RESAMPLE_BILINEAR;
      } else {
         ctx->set_error(ctx,400, "unknown value %s for node <resample_mode> (allowed values: nearest, bilinear", rule_node->txt);
         return;
      }
   }
}

mapcache_service* mapcache_service_wms_create(mapcache_context *ctx) {
   mapcache_service_wms* service = (mapcache_service_wms*)apr_pcalloc(ctx->pool, sizeof(mapcache_service_wms));
   if(!service) {
      ctx->set_error(ctx, 500, "failed to allocate wms service");
      return NULL;
   }
   service->forwarding_rules = apr_array_make(ctx->pool,0,sizeof(mapcache_forwarding_rule*));
   service->service.url_prefix = apr_pstrdup(ctx->pool,"");
   service->service.name = apr_pstrdup(ctx->pool,"wms");
   service->service.type = MAPCACHE_SERVICE_WMS;
   service->service.parse_request = _mapcache_service_wms_parse_request;
   service->service.create_capabilities_response = _create_capabilities_wms;
   service->service.configuration_parse_xml = _configuration_parse_wms_xml;
#ifdef ENABLE_UNMAINTAINED_JSON_PARSER
   service->service.configuration_parse_json = _configuration_parse_wms_json;
#endif
   service->getmap_strategy = MAPCACHE_GETMAP_ASSEMBLE;
   service->resample_mode = MAPCACHE_RESAMPLE_BILINEAR;
   service->getmap_format = NULL;
   return (mapcache_service*)service;
}

/** @} */
/* vim: ai ts=3 sts=3 et sw=3
*/
