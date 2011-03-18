/******************************************************************************
 * $id: mapfile.c 7854 2008-08-14 19:22:48Z dmorissette $
 *
 * Project:  MapServer
 * Purpose:  High level Map file parsing code.
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
 ****************************************************************************/

#define _GNU_SOURCE

#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

#include "mapserver.h"
#include "mapfile.h"
#include "mapthread.h"
#include "maptime.h"
#include "mapaxisorder.h"

#ifdef USE_GDAL
#  include "cpl_conv.h"
#  include "gdal.h"
#endif

MS_CVSID("$Id$")

extern int msyylex(void);
extern void msyyrestart(FILE *);
extern int msyylex_destroy(void);

extern double msyynumber;
extern int msyylineno;
extern FILE *msyyin;

extern int msyysource;
extern int msyystate;
extern char *msyystring;
extern char *msyybasepath;
extern int msyyreturncomments;
extern char *msyystring_buffer;
extern char msyystring_icase;

extern int loadSymbol(symbolObj *s, char *symbolpath); /* in mapsymbol.c */
extern void writeSymbol(symbolObj *s, FILE *stream); /* in mapsymbol.c */
static int loadGrid( layerObj *pLayer );
static int loadStyle(styleObj *style);
static void writeStyle(FILE* stream, int indent, styleObj *style);
static int msResolveSymbolNames(mapObj *map);


/************************************************************************/
/*                           int msIsAxisInverted                       */
/*      check to see if we shoudl invert the axis.                      */
/*                                                                      */
/************************************************************************/
static int msIsAxisInverted(int epsg_code)
{
    int i;
    /*check first in the static table*/
    for (i=0; i<AXIS_ORIENTATION_TABLE_SIZE; i++)
    {
        if (axisOrientationEpsgCodes[i].code == epsg_code)
          return axisOrientationEpsgCodes[i].inverted;
    }
    if ( epsg_code >=4000 &&  epsg_code < 5000)
      return MS_TRUE;

    return MS_FALSE;
    
}

/*
** Symbol to string static arrays needed for writing map files.
** Must be kept in sync with enumerations and defines found in mapserver.h.
*/
/* static char *msUnits[9]={"INCHES", "FEET", "MILES", "METERS", "KILOMETERS", "DD", "PIXELS", "PERCENTAGES", "NAUTICALMILES"}; */
/* static char *msLayerTypes[9]={"POINT", "LINE", "POLYGON", "RASTER", "ANNOTATION", "QUERY", "CIRCLE", "TILEINDEX","CHART"}; */
char *msPositionsText[MS_POSITIONS_LENGTH] = {"UL", "LR", "UR", "LL", "CR", "CL", "UC", "LC", "CC", "AUTO", "XY", "FOLLOW"}; /* msLabelPositions[] also used in mapsymbols.c (not static) */
/* static char *msBitmapFontSizes[5]={"TINY", "SMALL", "MEDIUM", "LARGE", "GIANT"}; */
/* static char *msQueryMapStyles[4]={"NORMAL", "HILITE", "SELECTED", "INVERTED"}; */
/* static char *msStatus[4]={"OFF", "ON", "DEFAULT", "EMBED"}; */
/* static char *msOnOff[2]={"OFF", "ON"}; */
/* static char *msTrueFalse[2]={"FALSE", "TRUE"}; */
/* static char *msYesNo[2]={"NO", "YES"}; */
/* static char *msJoinType[2]={"ONE-TO-ONE", "ONE-TO-MANY"}; */
/* static char *msAlignValue[3]={"LEFT","CENTER","RIGHT"}; */

/*
** Validates a string (value) against a series of patterns. We support up to four to allow cascading from classObj to
** layerObj to webObj plus a legacy pattern like TEMPLATEPATTERN or qstring_validation_pattern.
*/
int msValidateParameter(char *value, char *pattern1, char *pattern2, char *pattern3, char *pattern4) {
  if(msEvalRegex(pattern1, value) == MS_TRUE) return MS_SUCCESS;
  if(msEvalRegex(pattern2, value) == MS_TRUE) return MS_SUCCESS;
  if(msEvalRegex(pattern3, value) == MS_TRUE) return MS_SUCCESS;
  if(msEvalRegex(pattern4, value) == MS_TRUE) return MS_SUCCESS;

  msSetError(MS_REGEXERR, "Parameter pattern validation failed." , "msValidateParameter()");
  return(MS_FAILURE);
}

int msEvalRegex(char *e, char *s) {
  ms_regex_t re;

  if(!e || !s) return(MS_FALSE);

  if(ms_regcomp(&re, e, MS_REG_EXTENDED|MS_REG_NOSUB) != 0) {
    msSetError(MS_REGEXERR, "Failed to compile expression (%s).", "msEvalRegex()", e);   
    return(MS_FALSE);
  }
  
  if(ms_regexec(&re, s, 0, NULL, 0) != 0) { /* no match */
    ms_regfree(&re);
    msSetError(MS_REGEXERR, "String failed expression test.", "msEvalRegex()");
    return(MS_FALSE);
  }
  ms_regfree(&re);

  return(MS_TRUE);
}

void msFree(void *p) {
  if(p) free(p);
}

/*
** Free memory allocated for a character array
*/
void msFreeCharArray(char **array, int num_items)
{
  int i;

  if((num_items < 0) || !array) return;

  for(i=0;i<num_items;i++)
    msFree(array[i]);
  msFree(array);

  return;
}

/*
** Checks symbol from lexer against variable length list of
** legal symbols.
*/
int getSymbol(int n, ...) {
  int symbol;
  va_list argp;
  int i=0;

  symbol = msyylex();

  va_start(argp, n);
  while(i<n) { /* check each symbol in the list */
    if(symbol == va_arg(argp, int)) {
      va_end(argp);      
      return(symbol);
    }
    i++;
  }

  va_end(argp);

  msSetError(MS_SYMERR, "Parsing error near (%s):(line %d)", "getSymbol()", msyystring_buffer, msyylineno);  
  return(-1);
}

/*
** Get a string or symbol as a string.   Operates like getString(), but also
** supports symbols. 
*/
static char *getToken(void) {
  msyylex();  
  return msStrdup(msyystring_buffer);
}

/*
** Load a string from the map file. A "string" is defined in lexer.l.
*/
int getString(char **s) {
  /* if (*s)
    msSetError(MS_SYMERR, "Duplicate item (%s):(line %d)", "getString()", msyystring_buffer, msyylineno);
    return(MS_FAILURE);
  } else */
  if(msyylex() == MS_STRING) {
    if(*s) free(*s); /* avoid leak */
    *s = msStrdup(msyystring_buffer);
    if (*s == NULL) {
      msSetError(MS_MEMERR, NULL, "getString()");
      return(MS_FAILURE);
    }
    return(MS_SUCCESS);
  }

  msSetError(MS_SYMERR, "Parsing error near (%s):(line %d)", "getString()", msyystring_buffer, msyylineno);
  return(MS_FAILURE);
}

/*
** Load a floating point number from the map file. (see lexer.l)
*/
int getDouble(double *d) {
  if(msyylex() == MS_NUMBER) {
    *d = msyynumber;
    return(0); /* success */
  }

  msSetError(MS_SYMERR, "Parsing error near (%s):(line %d)", "getDouble()", msyystring_buffer, msyylineno); 
  return(-1);
}

/*
** Load a integer from the map file. (see lexer.l)
*/
int getInteger(int *i) {
  if(msyylex() == MS_NUMBER) {
    *i = (int)msyynumber;
    return(0); /* success */
  }

  msSetError(MS_SYMERR, "Parsing error near (%s):(line %d)", "getInteger()", msyystring_buffer, msyylineno); 
  return(-1);
}

int getCharacter(char *c) {
  if(msyylex() == MS_STRING) {
    *c = msyystring_buffer[0];
    return(0);
  }

  msSetError(MS_SYMERR, "Parsing error near (%s):(line %d)", "getCharacter()", msyystring_buffer, msyylineno); 
  return(-1);
}

/*
** Try to load as an integer, then try as a named symbol.
** Part of work on bug 490.
*/
int getIntegerOrSymbol(int *i, int n, ...) 
{
    int symbol;
    va_list argp;
    int j=0;
    
    symbol = msyylex();

    if (symbol == MS_NUMBER) {
        *i = (int)msyynumber;
        return MS_SUCCESS; /* success */
    }

    va_start(argp, n);
    while (j<n) { /* check each symbol in the list */
        if(symbol == va_arg(argp, int)) {
            va_end(argp);
            *i = symbol;
            return MS_SUCCESS;
        }
        j++;
    }
    va_end(argp);

    msSetError(MS_SYMERR, "Parsing error near (%s):(line %d)",
               "getIntegerOrSymbol()", msyystring_buffer, msyylineno); 
    return(-1);
}


/*
** msBuildPluginLibraryPath
**
** This function builds a path to be used dynamically to load plugin library.
*/
int msBuildPluginLibraryPath(char **dest, const char *lib_str, mapObj *map)
{
    char szLibPath[MS_MAXPATHLEN] = { '\0' };
    char szLibPathExt[MS_MAXPATHLEN] = { '\0' };
    const char *plugin_dir = msLookupHashTable( &(map->configoptions), "MS_PLUGIN_DIR");

    /* do nothing on windows, filename without .dll will be loaded by default*/
#if !defined(_WIN32)
    if (lib_str) {
        size_t len = strlen(lib_str);
        if (3 < len && strcmp(lib_str + len-3, ".so")) {
            strlcpy(szLibPathExt, lib_str, MS_MAXPATHLEN);
            strlcat(szLibPathExt, ".so", MS_MAXPATHLEN);
            lib_str = szLibPathExt;
        }
    }
#endif /* !defined(_WIN32) */
    if (NULL == msBuildPath(szLibPath, plugin_dir, lib_str)) {
        return MS_FAILURE;
    }
    *dest = msStrdup(szLibPath);

    return MS_SUCCESS;
}

/*
** Returns the index of specified symbol or -1 if not found.
**
** If try_addimage_if_notfound==MS_TRUE then msAddImageSymbol() will be called
** to try to allocate the symbol as an image symbol.
*/
int msGetSymbolIndex(symbolSetObj *symbols, char *name, int try_addimage_if_notfound)
{
  int i;

  if(!symbols || !name) return(-1);

  /* symbol 0 has no name */
  for(i=1; i<symbols->numsymbols; i++) {
    if(symbols->symbol[i]->name)
      if(strcasecmp(symbols->symbol[i]->name, name) == 0) return(i);
  }

  if (try_addimage_if_notfound)
    return(msAddImageSymbol(symbols, name)); /* make sure it's not a filename */

  return(-1);
}

/*
** Return the index number for a given layer based on its name.
*/
int msGetLayerIndex(mapObj *map, char *name)
{
  int i;

  if(!name) return(-1);

  for(i=0;i<map->numlayers; i++) {
    if(!GET_LAYER(map, i)->name) /* skip it */
      continue;
    if(strcmp(name, GET_LAYER(map, i)->name) == 0)
      return(i);
  }
  return(-1);
}

int msGetClassIndex(layerObj *layer, char *name)
{
  int i;

  if(!name) return(-1);

  for(i=0;i<layer->numclasses; i++) {
    if(!layer->class[i]->name) /* skip it */
      continue;
    if(strcmp(name, layer->class[i]->name) == 0)
      return(i);
  }
  return(-1);
}

int loadColor(colorObj *color, attributeBindingObj *binding) {
  int symbol;
  char hex[2];

  if(binding) {
    if((symbol = getSymbol(3, MS_NUMBER, MS_BINDING, MS_STRING)) == -1) return MS_FAILURE;
  } else {
    if((symbol = getSymbol(2, MS_NUMBER, MS_STRING)) == -1) return MS_FAILURE;
  }
  color->alpha=255;

  if(symbol == MS_NUMBER) {
    color->red = (int) msyynumber;
    if(getInteger(&(color->green)) == -1) return MS_FAILURE;
    if(getInteger(&(color->blue)) == -1) return MS_FAILURE;
  } else if(symbol == MS_STRING) {
    if(msyystring_buffer[0] == '#' && strlen(msyystring_buffer) == 7) { /* got a hex color */
      hex[0] = msyystring_buffer[1];
      hex[1] = msyystring_buffer[2];
      color->red = msHexToInt(hex);
      hex[0] = msyystring_buffer[3];
      hex[1] = msyystring_buffer[4];
      color->green = msHexToInt(hex);
      hex[0] = msyystring_buffer[5];
      hex[1] = msyystring_buffer[6];
      color->blue = msHexToInt(hex);
    } else {
       /* TODO: consider named colors here */
       msSetError(MS_SYMERR, "Invalid hex color (%s):(line %d)", "loadColor()", msyystring_buffer, msyylineno); 
       return MS_FAILURE;
    }
  } else {
    binding->item = msStrdup(msyystring_buffer);
    binding->index = -1;
  }

  return MS_SUCCESS;
}

#if ALPHACOLOR_ENABLED
int loadColorWithAlpha(colorObj *color) {
  char hex[2];

  if(getInteger(&(color->red)) == -1) {
    if(msyystring_buffer[0] == '#' && strlen(msyystring_buffer) == 7) { /* got a hex color */
      hex[0] = msyystring_buffer[1];
      hex[1] = msyystring_buffer[2];
      color->red = msHexToInt(hex);      
      hex[0] = msyystring_buffer[3];
      hex[1] = msyystring_buffer[4];
      color->green = msHexToInt(hex);
      hex[0] = msyystring_buffer[5];
      hex[1] = msyystring_buffer[6];
      color->blue = msHexToInt(hex);
      color->alpha = 0;

      return(MS_SUCCESS);
    }
    else if(msyystring_buffer[0] == '#' && strlen(msyystring_buffer) == 9) { /* got a hex color with alpha */
      hex[0] = msyystring_buffer[1];
      hex[1] = msyystring_buffer[2];
      color->red = msHexToInt(hex);      
      hex[0] = msyystring_buffer[3];
      hex[1] = msyystring_buffer[4];
      color->green = msHexToInt(hex);
      hex[0] = msyystring_buffer[5];
      hex[1] = msyystring_buffer[6];
      color->blue = msHexToInt(hex);
      hex[0] = msyystring_buffer[7];
      hex[1] = msyystring_buffer[8];
      color->alpha = msHexToInt(hex);
      return(MS_SUCCESS);
    }
    return(MS_FAILURE);
  }
  if(getInteger(&(color->green)) == -1) return(MS_FAILURE);
  if(getInteger(&(color->blue)) == -1) return(MS_FAILURE);
  if(getInteger(&(color->alpha)) == -1) return(MS_FAILURE);

  return(MS_SUCCESS);
}
#endif

/*
** Helper functions for writing mapfiles.
*/
static void writeLineFeed(FILE *stream) {
  fprintf(stream, "\n");
}

static void writeIndent(FILE *stream, int indent) {  
  const char *str="  "; /* change this string to define the indent */
  int i;
  for(i=0; i<indent; i++) fprintf(stream, "%s", str);
}

static void writeBlockBegin(FILE *stream, int indent, const char *name) {
  writeIndent(stream, indent);
  fprintf(stream, "%s\n", name);
}

static void writeBlockEnd(FILE *stream, int indent, const char *name) {
  writeIndent(stream, indent);
  fprintf(stream, "END # %s\n", name);
}

static void writeKeyword(FILE *stream, int indent, const char *name, int value, int size, ...) {
  va_list argp;
  int i, j=0;
  const char *s;

  va_start(argp, size);
  while (j<size) { /* check each value/keyword mapping in the list, values with no match are ignored */
    i = va_arg(argp, int);
    s = va_arg(argp, const char *);
    if(value == i) {
      writeIndent(stream, ++indent);
      fprintf(stream, "%s %s\n", name, s);
      va_end(argp);
      return;
    }
    j++;
  }
  va_end(argp);
}

static void writeDimension(FILE *stream, int indent, const char *name, int x, int y, char *bind_x, char *bind_y) {
  writeIndent(stream, ++indent);
  if(bind_x) fprintf(stream, "%s [%s] ", name, bind_x);
  else fprintf(stream, "%s %d ", name, x);
  if(bind_y) fprintf(stream, "[%s]\n", bind_y);
  else fprintf(stream, "%d\n", y);
}

static void writeExtent(FILE *stream, int indent, const char *name, rectObj extent) {
  if(!MS_VALID_EXTENT(extent)) return;
  writeIndent(stream, ++indent);
  fprintf(stream, "%s %.15g %.15g %.15g %.15g\n", name, extent.minx, extent.miny, extent.maxx, extent.maxy);
}

static void writeNumber(FILE *stream, int indent, const char *name, double defaultNumber, double number) {
  if(number == defaultNumber) return; /* don't output default */
  writeIndent(stream, ++indent);
  fprintf(stream, "%s %g\n", name, number);
}

static void writeCharacter(FILE *stream, int indent, const char *name, const char defaultCharacter, char character) {
  if(defaultCharacter == character) return;
  writeIndent(stream, ++indent);
  fprintf(stream, "%s '%c'\n", name, character);
}

static void writeString(FILE *stream, int indent, const char *name, const char *defaultString, char *string) {
  char *string_tmp;

  if(!string) return;
  if(defaultString && strcmp(string, defaultString) == 0) return;
  writeIndent(stream, ++indent);
  if(name) fprintf(stream, "%s ", name);
  if ( (strchr(string, '\'') == NULL) && (strchr(string, '\"') == NULL))
      fprintf(stream, "\"%s\"\n", string);
  else if ( (strchr(string, '\"') != NULL) && (strchr(string, '\'') == NULL))
      fprintf(stream, "'%s'\n", string);
  else if ( (strchr(string, '\'') != NULL) && (strchr(string, '\"') == NULL))
    fprintf(stream, "\"%s\"\n", string);
  else {
      string_tmp = msStringEscape(string);
      fprintf(stream, "\"%s\"\n", string_tmp);
      free(string_tmp);
  }   
}

static void writeNumberOrString(FILE *stream, int indent, const char *name, double defaultNumber, double number, char *string) {
  if(string)
    writeString(stream, indent, name, NULL, string);
  else
    writeNumber(stream, indent, name, defaultNumber, number);
}

static void writeNumberOrKeyword(FILE *stream, int indent, const char *name, double defaultNumber, double number, int value, int size, ...) {
  va_list argp;
  int i, j=0;
  const char *s;

  va_start(argp, size);
  while (j<size) { /* check each value/keyword mapping in the list */
    i = va_arg(argp, int);
    s = va_arg(argp, const char *);
    if(value == i) {
      writeIndent(stream, ++indent);
      fprintf(stream, "%s %s\n", name, s);
      va_end(argp);
      return;
    }
    j++;
  }
  va_end(argp);

  writeNumber(stream, indent, name, defaultNumber, number);
}

static void writeNameValuePair(FILE *stream, int indent, const char *name, const char *value) {
  char *string_tmp;
  if(!name || !value) return;
  writeIndent(stream, ++indent);  

  if ( (strchr(name, '\'') == NULL) && (strchr(name, '\"') == NULL))
      fprintf(stream, "\"%s\"\t", name);
  else if ( (strchr(name, '\"') != NULL) && (strchr(name, '\'') == NULL))
      fprintf(stream, "'%s'\t", name);
  else if ( (strchr(name, '\'') != NULL) && (strchr(name, '\"') == NULL))
    fprintf(stream, "\"%s\"\t", name);
  else {
      string_tmp = msStringEscape(name);
      fprintf(stream, "\"%s\"\t", string_tmp);
      free(string_tmp);
  }   

  if ( (strchr(value, '\'') == NULL) && (strchr(value, '\"') == NULL))
      fprintf(stream, "\"%s\"\n", value);
  else if ( (strchr(value, '\"') != NULL) && (strchr(value, '\'') == NULL))
      fprintf(stream, "'%s'\n", value);
  else if ( (strchr(value, '\'') != NULL) && (strchr(value, '\"') == NULL))
    fprintf(stream, "\"%s\"\n", value);
  else {
      string_tmp = msStringEscape(value);
      fprintf(stream, "\"%s\"\n", string_tmp);
      free(string_tmp);
  }   
}

static void writeAttributeBinding(FILE *stream, int indent, const char *name, attributeBindingObj *binding) {
  if(!binding || !binding->item) return;
  writeIndent(stream, ++indent);
  fprintf(stream, "%s [%s]\n", name, binding->item);
}

static void writeColor(FILE *stream, int indent, const char *name, colorObj *color) {
  if(!MS_VALID_COLOR(*color)) return;
  writeIndent(stream, ++indent);
#if ALPHACOLOR_ENABLED
  fprintf(stream, "%s %d %d %d\n", name, color->red, color->green, color->blue, color->alpha);
#else
  fprintf(stream, "%s %d %d %d\n", name, color->red, color->green, color->blue);
#endif
}

/* todo: deal with alpha's... */
static void writeColorRange(FILE *stream, int indent, const char *name, colorObj *mincolor, colorObj *maxcolor) {
  if(!MS_VALID_COLOR(*mincolor) || !MS_VALID_COLOR(*maxcolor)) return;
  writeIndent(stream, ++indent);
  fprintf(stream, "%s %d %d %d  %d %d %d\n", name, mincolor->red, mincolor->green, mincolor->blue, maxcolor->red, maxcolor->green, maxcolor->blue);
}

/*
** Initialize, load and free a single join
*/
void initJoin(joinObj *join)
{
  join->numitems = 0;

  join->name = NULL; /* unique join name, used for variable substitution */

  join->items = NULL; /* array to hold item names for the joined table */
  join->values = NULL; /* arrays of strings to holds one record worth of data */
 
  join->table = NULL;

  join->joininfo = NULL;

  join->from = NULL; /* join items */
  join->to = NULL;

  join->header = NULL;
  join->template = NULL; /* only html type templates are supported */
  join->footer = NULL;

  join->type = MS_JOIN_ONE_TO_ONE;

  join->connection = NULL;
  join->connectiontype = MS_DB_XBASE;
}

void freeJoin(joinObj *join) 
{
  msFree(join->name);
  msFree(join->table);
  msFree(join->from);
  msFree(join->to);

  msFree(join->header);
  msFree(join->template);
  msFree(join->footer);

  msFreeCharArray(join->items, join->numitems); /* these may have been free'd elsewhere */
  msFreeCharArray(join->values, join->numitems);
  join->numitems = 0;

  msJoinClose(join);
  msFree(join->connection);
}

int loadJoin(joinObj *join)
{
  initJoin(join);

  for(;;) {
    switch(msyylex()) {
    case(CONNECTION):
      if(getString(&join->connection) == MS_FAILURE) return(-1);
      break;
    case(CONNECTIONTYPE):
      if((join->connectiontype = getSymbol(4, MS_DB_XBASE, MS_DB_ORACLE, MS_DB_POSTGRES, MS_DB_CSV)) == -1) return(-1);
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadJoin()");
      return(-1);
    case(END):      
      if((join->from == NULL) || (join->to == NULL) || (join->table == NULL)) {
	msSetError(MS_EOFERR, "Join must define table, name, from and to properties.", "loadJoin()");
	return(-1);
      }
      if((join->type == MS_MULTIPLE) && ((join->template == NULL) || (join->name == NULL))) {
	msSetError(MS_EOFERR, "One-to-many joins must define template and name properties.", "loadJoin()");
	return(-1);
      }      
      return(0);
    case(FOOTER):
      if(getString(&join->footer) == MS_FAILURE) return(-1);
      break;
    case(FROM):
      if(getString(&join->from) == MS_FAILURE) return(-1);
      break;      
    case(HEADER):
      if(getString(&join->header) == MS_FAILURE) return(-1);
      break;
    case(JOIN):
      break; /* for string loads */
    case(NAME):
      if(getString(&join->name) == MS_FAILURE) return(-1);
      break;
    case(TABLE):
      if(getString(&join->table) == MS_FAILURE) return(-1);
      break;
    case(TEMPLATE):
      if(getString(&join->template) == MS_FAILURE) return(-1);
      break;
    case(TO):
      if(getString(&join->to) == MS_FAILURE) return(-1);
      break;
    case(TYPE):
      if((join->type = getSymbol(2, MS_JOIN_ONE_TO_ONE, MS_JOIN_ONE_TO_MANY)) == -1) return(-1);
      break;
    default:
      msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadJoin()", msyystring_buffer, msyylineno);
      return(-1);
    }
  } /* next token */
}

static void writeJoin(FILE *stream, int indent, joinObj *join)
{
  indent++;
  writeBlockBegin(stream, indent, "JOIN");
  writeString(stream, indent, "FOOTER", NULL, join->footer);
  writeString(stream, indent, "FROM", NULL, join->from);
  writeString(stream, indent, "HEADER", NULL, join->header);
  writeString(stream, indent, "NAME", NULL, join->name);
  writeString(stream, indent, "TABLE", NULL, join->table);
  writeString(stream, indent, "TEMPLATE", NULL, join->template);
  writeString(stream, indent, "TO", NULL, join->to);
  writeKeyword(stream, indent, "CONNECTIONTYPE", join->connectiontype, 2, MS_DB_CSV, "CSV", MS_DB_POSTGRES, "POSTRESQL");
  writeKeyword(stream, indent, "TYPE", join->type, 1, MS_JOIN_ONE_TO_MANY, "ONE-TO-MANY");
  writeBlockEnd(stream, indent, "JOIN");
}

/* inserts a feature at the end of the list, can create a new list */
featureListNodeObjPtr insertFeatureList(featureListNodeObjPtr *list, shapeObj *shape)
{
  featureListNodeObjPtr node;

  node = (featureListNodeObjPtr) malloc(sizeof(featureListNodeObj));
  MS_CHECK_ALLOC(node, sizeof(featureListNodeObj), NULL);

  msInitShape(&(node->shape));
  if(msCopyShape(shape, &(node->shape)) == -1) return(NULL);

  /* AJS - alans@wunderground.com O(n^2) -> O(n) conversion, keep a pointer to the end */

  /* set the tailifhead to NULL, since it is only set for the head of the list */
  node->tailifhead = NULL;
  node->next = NULL;

  /* if we are at the head of the list, we need to set the list to node, before pointing tailifhead somewhere  */
  if(*list == NULL) {
    *list=node;
  } else {      
    if((*list)->tailifhead!=NULL) /* this should never be NULL, but just in case */
      (*list)->tailifhead->next=node; /* put the node at the end of the list */
  }

  /* repoint the head of the list to the end  - our new element
     this causes a loop if we are at the head, be careful not to 
     walk in a loop */
  (*list)->tailifhead = node;
 
  return(node); /* a pointer to last object in the list */
}

void freeFeatureList(featureListNodeObjPtr list)
{
    featureListNodeObjPtr listNext = NULL;
    while (list!=NULL)
    {
        listNext = list->next;
        msFreeShape(&(list->shape));
        msFree(list);
        list = listNext;
    }
}

/* lineObj = multipointObj */
static int loadFeaturePoints(lineObj *points)
{
  int buffer_size=0;

  points->point = (pointObj *)malloc(sizeof(pointObj)*MS_FEATUREINITSIZE);
  MS_CHECK_ALLOC(points->point, sizeof(pointObj)*MS_FEATUREINITSIZE, MS_FAILURE);
  points->numpoints = 0;
  buffer_size = MS_FEATUREINITSIZE;

  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadFeaturePoints()");      
      return(MS_FAILURE);
    case(END):
      return(MS_SUCCESS);
    case(MS_NUMBER):
      if(points->numpoints == buffer_size) { /* just add it to the end */
	points->point = (pointObj *) realloc(points->point, sizeof(pointObj)*(buffer_size+MS_FEATUREINCREMENT));
        MS_CHECK_ALLOC(points->point, sizeof(pointObj)*(buffer_size+MS_FEATUREINCREMENT), MS_FAILURE);
	buffer_size+=MS_FEATUREINCREMENT;
      }

      points->point[points->numpoints].x = atof(msyystring_buffer);
      if(getDouble(&(points->point[points->numpoints].y)) == -1) return(MS_FAILURE);

      points->numpoints++;
      break;
    default:
      msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadFeaturePoints()",  msyystring_buffer, msyylineno );
      return(MS_FAILURE);
    }
  }
}

static int loadFeature(layerObj	*player, int type)
{
  int status=MS_SUCCESS;
  featureListNodeObjPtr *list = &(player->features);
  featureListNodeObjPtr node;
  multipointObj points={0,NULL};
  shapeObj *shape=NULL;

  shape = (shapeObj *) malloc(sizeof(shapeObj));
  MS_CHECK_ALLOC(shape, sizeof(shapeObj), MS_FAILURE);

  msInitShape(shape);
  shape->type = type;

  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadFeature()");      
      return(MS_FAILURE);
    case(END):
      if(player->features != NULL && player->features->tailifhead != NULL) 
            shape->index = player->features->tailifhead->shape.index + 1;
        else 
            shape->index = 0;
      if((node = insertFeatureList(list, shape)) == NULL) 
	  status = MS_FAILURE;

      msFreeShape(shape); /* clean up */
      msFree(shape);

      return(status);
    case(FEATURE):
      break; /* for string loads */
    case(POINTS):
      if(loadFeaturePoints(&points) == MS_FAILURE) return(MS_FAILURE); /* no clean up necessary, just return */
      status = msAddLine(shape, &points);

      msFree(points.point); /* clean up */
      points.numpoints = 0;

      if(status == MS_FAILURE) return(MS_FAILURE);
      break;
    case(ITEMS):
    {
      char *string=NULL;
      if(getString(&string) == MS_FAILURE) return(MS_FAILURE);
      if (string)
      {
        if(shape->values) msFreeCharArray(shape->values, shape->numvalues);
        shape->values = msStringSplit(string, ';', &shape->numvalues);
        msFree(string); /* clean up */
      }
      break;
    }
    case(TEXT):
      if(getString(&shape->text) == MS_FAILURE) return(MS_FAILURE);
      break;
    case(WKT):
      {
	char *string=NULL;

	/* todo, what do we do with multiple WKT property occurances? */
	
	if(getString(&string) == MS_FAILURE) return(MS_FAILURE);
	if((shape = msShapeFromWKT(string)) == NULL)
	  status = MS_FAILURE;

	msFree(string); /* clean up */

	if(status == MS_FAILURE) return(MS_FAILURE);
	break;
      }
    default:
      msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadfeature()", msyystring_buffer, msyylineno);
      return(MS_FAILURE);
    }
  } /* next token */  
}

static void writeFeature(FILE *stream, int indent, shapeObj *feature) 
{
  int i,j;

  indent++;
  writeBlockBegin(stream, indent, "FEATURE");

  indent++;
  for(i=0; i<feature->numlines; i++) {
    writeBlockBegin(stream, indent, "POINTS");
    for(j=0; j<feature->line[i].numpoints; j++) {
      writeIndent(stream, indent);
      fprintf(stream, "%.15g %.15g\n", feature->line[i].point[j].x, feature->line[i].point[j].y);
    }
    writeBlockEnd(stream, indent, "POINTS");
  }
  indent--;

  if (feature->numvalues) {
    writeIndent(stream, indent);
    fprintf(stream, "ITEMS \"");
    for (i=0; i<feature->numvalues; i++) {
      if (i == 0)
        fprintf(stream, "%s", feature->values[i]);
      else
        fprintf(stream, ";%s", feature->values[i]);
    }
    fprintf(stream, "\"\n");
  }

  writeString(stream, indent, "TEXT", NULL, feature->text);
  writeBlockEnd(stream, indent, "FEATURE");
}

void initGrid( graticuleObj *pGraticule )
{
  memset( pGraticule, 0, sizeof( graticuleObj ) );
}

static int loadGrid( layerObj *pLayer )
{
  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadGrid()");     
      return(-1);      
    case(END):
      return(0);
    case(GRID):
      break; /* for string loads */
    case( LABELFORMAT ):
      if(getString(&((graticuleObj *)pLayer->layerinfo)->labelformat) == MS_FAILURE) {
	if(strcasecmp(msyystring_buffer, "DD") == 0) /* DD triggers a symbol to be returned instead of a string so check for this special case */
	  ((graticuleObj *)pLayer->layerinfo)->labelformat = msStrdup("DD");
        else
	  return(-1);
      }
      break;
    case( MINARCS ):
      if(getDouble(&((graticuleObj *)pLayer->layerinfo)->minarcs) == -1) 
	return(-1);      
      break;      
    case( MAXARCS ):
      if(getDouble(&((graticuleObj *)pLayer->layerinfo)->maxarcs) == -1) 
	return(-1);      
      break;
    case( MININTERVAL ):
      if(getDouble(&((graticuleObj *)pLayer->layerinfo)->minincrement) == -1) 
	return(-1);      
      break;      
    case( MAXINTERVAL ):
      if(getDouble(&((graticuleObj *)pLayer->layerinfo)->maxincrement) == -1) 
	return(-1);      
      break;
    case( MINSUBDIVIDE ):
      if(getDouble(&((graticuleObj *)pLayer->layerinfo)->minsubdivides) == -1) 
	return(-1);      
      break;      
    case( MAXSUBDIVIDE ):
      if(getDouble(&((graticuleObj *)pLayer->layerinfo)->maxsubdivides) == -1) 
	return(-1);
      break;				
    default:
      msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadGrid()", msyystring_buffer, msyylineno);          
      return(-1);      
    }
  }
}
  
static void writeGrid(FILE *stream, int indent, graticuleObj *pGraticule)
{
  if(!pGraticule) return;

  indent++;
  writeBlockBegin(stream, indent, "GRID");
  writeString(stream, indent, "LABELFORMAT", NULL, pGraticule->labelformat);
  writeNumber(stream, indent, "MAXARCS", 0, pGraticule->maxarcs);
  writeNumber(stream, indent, "MAXSUBDIVIDE", 0, pGraticule->maxsubdivides);
  writeNumber(stream, indent, "MAXINTERVAL", 0, pGraticule->maxincrement);
  writeNumber(stream, indent, "MINARCS", 0, pGraticule->minarcs);
  writeNumber(stream, indent, "MININTERVAL", 0, pGraticule->minincrement);
  writeNumber(stream, indent, "MINSUBDIVIDE", 0, pGraticule->minsubdivides);
  writeBlockEnd(stream, indent, "GRID");
}

/*
** Initialize, load and free a projectionObj structure
*/
int msInitProjection(projectionObj *p)
{
  p->gt.need_geotransform = MS_FALSE;
  p->numargs = 0;
  p->args = NULL;
#ifdef USE_PROJ  
  p->proj = NULL;
  p->args = (char **)malloc(MS_MAXPROJARGS*sizeof(char *));
  MS_CHECK_ALLOC(p->args, MS_MAXPROJARGS*sizeof(char *), -1);
#endif
  return(0);
}

void msFreeProjection(projectionObj *p) {
#ifdef USE_PROJ
  if(p->proj)
  {
      pj_free(p->proj);
      p->proj = NULL;
  }

  msFreeCharArray(p->args, p->numargs);  
  p->args = NULL;
  p->numargs = 0;
#endif
}

/*
** Handle OGC WMS/WFS AUTO projection in the format:
**    "AUTO:proj_id,units_id,lon0,lat0"
*/
#ifdef USE_PROJ    
static int _msProcessAutoProjection(projectionObj *p)
{
    char **args;
    int numargs, nProjId, nUnitsId, nZone;
    double dLat0, dLon0;
    const char *pszUnits = "m";
    char szProjBuf[512]="";

    /* WMS/WFS AUTO projection: "AUTO:proj_id,units_id,lon0,lat0" */
    args = msStringSplit(p->args[0], ',', &numargs);
    if (numargs != 4 || 
        (strncasecmp(args[0], "AUTO:", 5) != 0 &&
         strncasecmp(args[0], "AUTO2:", 6) != 0))
    {
        msSetError(MS_PROJERR, 
                   "WMS/WFS AUTO/AUTO2 PROJECTION must be in the format "
                   "'AUTO:proj_id,units_id,lon0,lat0' or 'AUTO2:crs_id,factor,lon0,lat0'(got '%s').\n",
                   "_msProcessAutoProjection()", p->args[0]);
        return -1;
    }

    if (strncasecmp(args[0], "AUTO:", 5)==0)
      nProjId = atoi(args[0]+5);
    else
      nProjId = atoi(args[0]+6);

    nUnitsId = atoi(args[1]);
    dLon0 = atof(args[2]);
    dLat0 = atof(args[3]);


    /*There is no unit parameter for AUTO2. The 2nd parameter is
     factor. Set the units to always be meter*/
    if (strncasecmp(args[0], "AUTO2:", 6) == 0)
      nUnitsId = 9001;

    msFreeCharArray(args, numargs);

    /* Handle EPSG Units.  Only meters for now. */
    switch(nUnitsId)
    {
      case 9001:  /* Meters */
        pszUnits = "m";
        break;
      default:
        msSetError(MS_PROJERR, 
                   "WMS/WFS AUTO PROJECTION: EPSG Units %d not supported.\n",
                   "_msProcessAutoProjection()", nUnitsId);
        return -1;
    }

    /* Build PROJ4 definition.
     * This is based on the definitions found in annex E of the WMS 1.1.1 
     * spec and online at http://www.digitalearth.org/wmt/auto.html
     * The conversion from the original WKT definitions to PROJ4 format was
     * done using the MapScript setWKTProjection() function (based on OGR).
     */
    switch(nProjId)
    {
      case 42001: /** WGS 84 / Auto UTM **/
        nZone = (int) floor( (dLon0 + 180.0) / 6.0 ) + 1;
        sprintf( szProjBuf, 
                 "+proj=tmerc+lat_0=0+lon_0=%.16g+k=0.999600+x_0=500000"
                 "+y_0=%.16g+ellps=WGS84+datum=WGS84+units=%s", 
                 -183.0 + nZone * 6.0, 
                 (dLat0 >= 0.0) ? 0.0 : 10000000.0,
                 pszUnits);
        break;
      case 42002: /** WGS 84 / Auto Tr. Mercator **/
        sprintf( szProjBuf, 
                 "+proj=tmerc+lat_0=0+lon_0=%.16g+k=0.999600+x_0=500000"
                 "+y_0=%.16g+ellps=WGS84+datum=WGS84+units=%s",
                 dLon0, 
                 (dLat0 >= 0.0) ? 0.0 : 10000000.0,
                 pszUnits);
        break;
      case 42003: /** WGS 84 / Auto Orthographic **/
        sprintf( szProjBuf, 
                 "+proj=ortho+lon_0=%.16g+lat_0=%.16g+x_0=0+y_0=0"
                 "+ellps=WGS84+datum=WGS84+units=%s",
                 dLon0, dLat0, pszUnits );
        break;
      case 42004: /** WGS 84 / Auto Equirectangular **/
        /* Note that we have to pass lon_0 as lon_ts for this one to */
        /* work.  Either a PROJ4 bug or a PROJ4 documentation issue. */
        sprintf( szProjBuf, 
                 "+proj=eqc+lon_ts=%.16g+lat_ts=%.16g+x_0=0+y_0=0"
                 "+ellps=WGS84+datum=WGS84+units=%s",
                 dLon0, dLat0, pszUnits);
        break;
      case 42005: /** WGS 84 / Auto Mollweide **/
        sprintf( szProjBuf, 
                 "+proj=moll+lon_0=%.16g+x_0=0+y_0=0+ellps=WGS84"
                 "+datum=WGS84+units=%s",
                 dLon0, pszUnits);
        break;
      default:
        msSetError(MS_PROJERR, 
                   "WMS/WFS AUTO PROJECTION %d not supported.\n",
                   "_msProcessAutoProjection()", nProjId);
        return -1;
    }

    /* msDebug("%s = %s\n", p->args[0], szProjBuf); */

    /* OK, pass the definition to pj_init() */
    args = msStringSplit(szProjBuf, '+', &numargs);

    msAcquireLock( TLOCK_PROJ );
    if( !(p->proj = pj_init(numargs, args)) ) {
        int *pj_errno_ref = pj_get_errno_ref();
        msReleaseLock( TLOCK_PROJ );
        msSetError(MS_PROJERR, pj_strerrno(*pj_errno_ref), 
                   "msProcessProjection()");	  
        return(-1);
    }
    
    msReleaseLock( TLOCK_PROJ );

    msFreeCharArray(args, numargs);

    return(0);
}
#endif /* USE_PROJ */

int msProcessProjection(projectionObj *p)
{
#ifdef USE_PROJ    
    assert( p->proj == NULL );
    
    if( strcasecmp(p->args[0],"GEOGRAPHIC") == 0 ) {
        msSetError(MS_PROJERR, 
                   "PROJECTION 'GEOGRAPHIC' no longer supported.\n"
                   "Provide explicit definition.\n"
                   "ie. proj=latlong\n"
                   "    ellps=clrk66\n",
                   "msProcessProjection()");	  
        return(-1);
    }

    if (strcasecmp(p->args[0], "AUTO") == 0) {
	p->proj = NULL;
        return 0;
    }

    if (strncasecmp(p->args[0], "AUTO:", 5) == 0 ||
        strncasecmp(p->args[0], "AUTO2:", 6) == 0)
    {
        /* WMS/WFS AUTO projection: "AUTO:proj_id,units_id,lon0,lat0" */
        /*WMS 1.3.0: AUTO2:auto_crs_id,factor,lon0,lat0*/
        return _msProcessAutoProjection(p);
    }
    msAcquireLock( TLOCK_PROJ );
    if( !(p->proj = pj_init(p->numargs, p->args)) ) {
        int *pj_errno_ref = pj_get_errno_ref();
        msReleaseLock( TLOCK_PROJ );
        msSetError(MS_PROJERR, pj_strerrno(*pj_errno_ref), 
                   "msProcessProjection()");	  
        return(-1);
    }
    
    msReleaseLock( TLOCK_PROJ );

    return(0);
#else
  msSetError(MS_PROJERR, "Projection support is not available.", 
             "msProcessProjection()");
  return(-1);
#endif
}

static int loadProjection(projectionObj *p)
{
#ifdef USE_PROJ
  int i=0;
#endif

  p->gt.need_geotransform = MS_FALSE;

#ifdef USE_PROJ

  if ( p->proj != NULL )
  {
      msSetError(MS_MISCERR, "Projection is already initialized. Multiple projection definitions are not allowed in this object. (line %d)", 
                   "loadProjection()", msyylineno);	  
        return(-1);
  }

  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadProjection()");      
      return(-1);
    case(END):
      if( i == 1 && strstr(p->args[0],"+") != NULL )
      {
          char *one_line_def = p->args[0];
          int result;

          p->args[0] = NULL;
          result = msLoadProjectionString( p, one_line_def );
          free( one_line_def );
          return result;
      }
      else
      {
      p->numargs = i;
      if(p->numargs != 0)
          return msProcessProjection(p);
      else
          return 0;
      }
      break;
    case(MS_STRING):
    case(MS_AUTO):
      p->args[i] = msStrdup(msyystring_buffer);
      p->automatic = MS_TRUE;
      i++;
      break;
    default:
      msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadProjection()",
                 msyystring_buffer, msyylineno);
      return(-1);
    }
  } /* next token */
#else
  msSetError(MS_PROJERR, "Projection support is not available.", "loadProjection()");
  return(-1);
#endif
}


/************************************************************************/
/*                         msLoadProjectionStringEPSG                   */
/*                                                                      */
/*      Checks fro EPSG type projection and set the axes for a          */
/*      certain code ranges.                                            */
/*      Use for now in WMS 1.3.0                                        */
/************************************************************************/
int msLoadProjectionStringEPSG(projectionObj *p, const char *value)
{
#ifdef USE_PROJ
   if(p) msFreeProjection(p);

    p->gt.need_geotransform = MS_FALSE;
    
    if (strncasecmp(value, "EPSG:", 5) == 0)
    {
        size_t buffer_size = 10 + strlen(value+5) + 1;
        char *init_string = (char*)msSmallMalloc(buffer_size);

        /* translate into PROJ.4 format. */
        snprintf(init_string, buffer_size, "init=epsg:%s", value+5 );

        p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
        p->args[0] = init_string;
        p->numargs = 1;
        

        if( msIsAxisInverted(atoi(value+5)))
        {
            p->args[1] = msStrdup("+epsgaxis=ne");
            p->numargs = 2;
        }

        return msProcessProjection( p );
    }

    return msLoadProjectionString(p, value);

#else
  msSetError(MS_PROJERR, "Projection support is not available.", 
             "msLoadProjectionStringEPSG()");
  return(-1);
#endif
}


int msLoadProjectionString(projectionObj *p, const char *value)
{
  p->gt.need_geotransform = MS_FALSE;

#ifdef USE_PROJ
  if(p) msFreeProjection(p);


  /*
   * Handle new style definitions, the same as they would be given to
   * the proj program.  
   * eg. 
   *    "+proj=utm +zone=11 +ellps=WGS84"
   */
  if( value[0] == '+' )
  {
      char 	*trimmed;
      int	i, i_out=0;

      trimmed = msStrdup(value+1);
      for( i = 1; value[i] != '\0'; i++ )
      {
          if( !isspace( value[i] ) )
              trimmed[i_out++] = value[i];
      }
      trimmed[i_out] = '\0';
      
      p->args = msStringSplit(trimmed,'+', &p->numargs);
      free( trimmed );
  }
  else if (strncasecmp(value, "AUTO:", 5) == 0 ||
           strncasecmp(value, "AUTO2:", 6) == 0)
  {
     /* WMS/WFS AUTO projection: "AUTO:proj_id,units_id,lon0,lat0" */
     /* WMS 1.3.0 projection: "AUTO2:auto_crs_id,factor,lon0,lat0"*/
     /* Keep the projection defn into a single token for writeProjection() */
     /* to work fine. */
      p->args = (char**)msSmallMalloc(sizeof(char*));
      p->args[0] = msStrdup(value);
      p->numargs = 1;
  }
  else if (strncasecmp(value, "EPSG:", 5) == 0)
  {
      size_t buffer_size = 10 + strlen(value+5) + 1;
      char *init_string = (char*)msSmallMalloc(buffer_size);

      /* translate into PROJ.4 format. */
      snprintf( init_string, buffer_size, "init=epsg:%s", value+5);

      p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
      p->args[0] = init_string;
      p->numargs = 1;
  }
  else if (strncasecmp(value, "urn:ogc:def:crs:EPSG:",21) == 0)
  { /* this is very preliminary urn support ... expand later */ 
      size_t buffer_size = 0;
      char *init_string =  NULL;
      const char *code;

      code = value + 21;

      while( *code != ':' && *code != '\0' )
          code++;
      if( *code == ':' )
          code++;

      buffer_size = 10 + strlen(code) + 1;
      init_string = (char*)msSmallMalloc(buffer_size);

      /* translate into PROJ.4 format. */
      snprintf( init_string, buffer_size, "init=epsg:%s", code );

      p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
      p->args[0] = init_string;
      p->numargs = 1;

      if( atoi(code) >= 4000 && atoi(code) < 5000 )
      {
          p->args[1] = msStrdup("+epsgaxis=ne");
          p->numargs = 2;
      }
  }
  else if (strncasecmp(value, "urn:x-ogc:def:crs:EPSG:",23) == 0)
  { /*this case is to account for OGC CITE tests where x-ogc was used
      before the ogc name became an official NID. Note also we also account
      for the fact that a space for the version of the espg is not used with CITE tests. 
      (Syntax used could be urn:ogc:def:objectType:authority:code)*/ 
    
      size_t buffer_size = 0;
      char *init_string =  NULL;
      const char *code;

      if (value[23] == ':')
        code = value + 23;
      else
        code = value + 22;

      while( *code != ':' && *code != '\0' )
          code++;
      if( *code == ':' )
          code++;

      buffer_size = 10 + strlen(code) + 1;
      init_string = (char*)msSmallMalloc(buffer_size);

      /* translate into PROJ.4 format. */
      snprintf( init_string, buffer_size, "init=epsg:%s", code );

      p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
      p->args[0] = init_string;
      p->numargs = 1;

      if( atoi(code) >= 4000 && atoi(code) < 5000 )
      {
          p->args[1] = msStrdup("+epsgaxis=ne");
          p->numargs = 2;
      }
    
  }
  else if (strncasecmp(value, "urn:ogc:def:crs:OGC:",20) == 0 )
  { /* this is very preliminary urn support ... expand later */ 
      char init_string[100];
      const char *id;

      id = value + 20;
      while( *id != ':' && *id == '\0' )
          id++;

      if( *id == ':' )
          id++;

      init_string[0] = '\0';

      if( strcasecmp(id,"CRS84") == 0 )
          strncpy( init_string, "init=epsg:4326", sizeof(init_string) );
      else if( strcasecmp(id,"CRS83") == 0 )
          strncpy( init_string, "init=epsg:4269", sizeof(init_string) );
      else if( strcasecmp(id,"CRS27") == 0 )
          strncpy( init_string, "init=epsg:4267", sizeof(init_string) );
      else
      {
          msSetError( MS_PROJERR, 
                      "Unrecognised OGC CRS def '%s'.",
                      "msLoadProjectionString()", 
                      value );
          return -1;
      }

      p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
      p->args[0] = msStrdup(init_string);
      p->numargs = 1;
  }
  /* URI projection support */
  else if (EQUALN("http://www.opengis.net/def/crs/EPSG/", value, 36))
  { /* this is very preliminary urn support ... expand later */
      char init_string[100];
      const char *code;

      code = value + 36;
      while( *code != '/' && *code != '\0' )
          code++;
      if( *code == '/' )
          code++;

      /* translate into PROJ.4 format. */
      snprintf( init_string, sizeof(init_string), "init=epsg:%s", code );

      p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
      p->args[0] = msStrdup(init_string);
      p->numargs = 1;

      if( atoi(code) >= 4000 && atoi(code) < 5000 )
      {
          p->args[1] = msStrdup("+epsgaxis=ne");
          p->numargs = 2;
      }
  }
  else if (EQUALN("http://www.opengis.net/def/crs/OGC/", value, 35) )
  {
      char init_string[100];
      const char *id;

      id = value + 35;
      while( *id != '/' && *id == '\0' )
          id++;
      if( *id == '/' )
          id++;

      init_string[0] = '\0';

      if( strcasecmp(id,"CRS84") == 0 )
          strncpy( init_string, "init=epsg:4326", sizeof(init_string) );
      else if( strcasecmp(id,"CRS83") == 0 )
          strncpy( init_string, "init=epsg:4269", sizeof(init_string) );
      else if( strcasecmp(id,"CRS27") == 0 )
          strncpy( init_string, "init=epsg:4267", sizeof(init_string) );
      else
      {
          msSetError( MS_PROJERR,
                      "Unrecognised OGC CRS def '%s'.",
                      "msLoadProjectionString()",
                      value );
          return -1;
      }

      p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
      p->args[0] = msStrdup(init_string);
      p->numargs = 1;
  }
  else if (strncasecmp(value, "CRS:",4) == 0 )
  {
      char init_string[100];
      init_string[0] = '\0';
      if (atoi(value+4) == 84)
        strncpy( init_string, "init=epsg:4326", sizeof(init_string) );
      else if (atoi(value+4) == 83)
        strncpy( init_string, "init=epsg:4269", sizeof(init_string) );
      else if (atoi(value+4) == 27)
        strncpy( init_string, "init=epsg:4267", sizeof(init_string) );
      else
      {
          msSetError( MS_PROJERR, 
                      "Unrecognised OGC CRS def '%s'.",
                      "msLoadProjectionString()", 
                      value );
          return -1;
      }
      p->args = (char**)msSmallMalloc(sizeof(char*) * 2);
      p->args[0] = msStrdup(init_string);
      p->numargs = 1;
  }
  /*
   * Handle old style comma delimited.  eg. "proj=utm,zone=11,ellps=WGS84".
   */
  else
  {
      p->args = msStringSplit(value,',', &p->numargs);
  }

  return msProcessProjection( p );
#else
  msSetError(MS_PROJERR, "Projection support is not available.", 
             "msLoadProjectionString()");
  return(-1);
#endif
}

static void writeProjection(FILE *stream, int indent, projectionObj *p) {
#ifdef USE_PROJ  
  int i;

  if(!p || p->numargs <= 0) return;
  indent++;
  writeBlockBegin(stream, indent, "PROJECTION");
  for(i=0; i<p->numargs; i++)
    writeString(stream, indent, NULL, NULL, p->args[i]);
  writeBlockEnd(stream, indent, "PROJECTION");
#endif
}

/*
** Initialize, load and free a labelObj structure
*/
void initLabel(labelObj *label)
{
  int i;

  label->antialias = -1; /* off  */
  label->align = MS_ALIGN_LEFT;
  MS_INIT_COLOR(label->color, 0,0,0,255);  
  MS_INIT_COLOR(label->outlinecolor, -1,-1,-1,255); /* don't use it */
  label->outlinewidth=1;

  MS_INIT_COLOR(label->shadowcolor, -1,-1,-1,255); /* don't use it */
  label->shadowsizex = label->shadowsizey = 1;

  label->font = NULL;
  label->type = MS_BITMAP;
  label->size = MS_MEDIUM;

  label->position = MS_CC;
  label->angle = 0;
  label->anglemode = MS_NONE;
  label->minsize = MS_MINFONTSIZE;
  label->maxsize = MS_MAXFONTSIZE;
  label->buffer = 0;
  label->offsetx = label->offsety = 0;
  label->minscaledenom=-1;
  label->maxscaledenom=-1;
  label->minfeaturesize = -1; /* no limit */
  label->autominfeaturesize = MS_FALSE;
  label->mindistance = -1; /* no limit */
  label->repeatdistance = 0; /* no repeat */
  label->maxoverlapangle = 22.5; /* default max overlap angle */
  label->partials = MS_TRUE;
  label->wrap = '\0';
  label->maxlength = 0;
  label->minlength = 0;
  label->space_size_10=0.0;

  label->encoding = NULL;

  label->force = MS_FALSE;
  label->priority = MS_DEFAULT_LABEL_PRIORITY;

  /* Set maxstyles = 0, styles[] will be allocated as needed on first call 
   * to msGrowLabelStyles()
   */
  label->numstyles = label->maxstyles = 0;   
  label->styles = NULL;  

  label->numbindings = 0;
  for(i=0; i<MS_LABEL_BINDING_LENGTH; i++) {
    label->bindings[i].item = NULL;
    label->bindings[i].index = -1;
  }

  return;
}

static void freeLabel(labelObj *label)
{
  int i;

  msFree(label->font);
  msFree(label->encoding);

  for(i=0;i<label->numstyles;i++) { /* each style */
    if(label->styles[i]!=NULL) {
      if(freeStyle(label->styles[i]) == MS_SUCCESS) {
        msFree(label->styles[i]);
      }
    }
  }
  msFree(label->styles);

  for(i=0; i<MS_LABEL_BINDING_LENGTH; i++)
    msFree(label->bindings[i].item);
}

static int loadLabel(labelObj *label)
{
  int symbol;

  for(;;) {
    switch(msyylex()) {
    case(ANGLE):
      if((symbol = getSymbol(5, MS_NUMBER,MS_AUTO,MS_AUTO2,MS_FOLLOW,MS_BINDING)) == -1) 
	return(-1);

      if(symbol == MS_NUMBER)
	label->angle = msyynumber;
      else if(symbol == MS_BINDING) {
        if (label->bindings[MS_LABEL_BINDING_ANGLE].item != NULL)
          msFree(label->bindings[MS_LABEL_BINDING_ANGLE].item);
        label->bindings[MS_LABEL_BINDING_ANGLE].item = msStrdup(msyystring_buffer);
        label->numbindings++;
      } else if ( symbol == MS_FOLLOW ) {
#ifndef GD_HAS_FTEX_XSHOW
	msSetError(MS_IDENTERR, "Keyword FOLLOW is not valid without TrueType font support and GD version 2.0.29 or higher.", "loadlabel()");
	return(-1);
#else 
        label->anglemode = MS_FOLLOW;         
#endif
      } else if ( symbol == MS_AUTO2 ) {
	label->anglemode = MS_AUTO2;
      } else
	label->anglemode = MS_AUTO;
      break;
    case(ALIGN):
      if((label->align = getSymbol(3, MS_ALIGN_LEFT,MS_ALIGN_CENTER,MS_ALIGN_RIGHT)) == -1) return(-1);
      break;
    case(ANTIALIAS):
      if((label->antialias = getSymbol(2, MS_TRUE,MS_FALSE)) == -1) 
	return(-1);
      break;
    case(BUFFER):
      if(getInteger(&(label->buffer)) == -1) return(-1);
      break;
    case(COLOR): 
      if(loadColor(&(label->color), &(label->bindings[MS_LABEL_BINDING_COLOR])) != MS_SUCCESS) return(-1);      
      if(label->bindings[MS_LABEL_BINDING_COLOR].item) label->numbindings++;
      break;
    case(ENCODING):
      if((getString(&label->encoding)) == MS_FAILURE) return(-1);
      break;
    case(END):
      return(0);
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadLabel()");      
      return(-1);
    case(FONT):
#if defined (USE_GD_TTF) || defined (USE_GD_FT)
      if((symbol = getSymbol(2, MS_STRING, MS_BINDING)) == -1)
        return(-1);

      if(symbol == MS_STRING) {
        if (label->font != NULL)
          msFree(label->font);
        label->font = msStrdup(msyystring_buffer);
      } else {
        if (label->bindings[MS_LABEL_BINDING_FONT].item != NULL)
          msFree(label->bindings[MS_LABEL_BINDING_FONT].item);
        label->bindings[MS_LABEL_BINDING_FONT].item = msStrdup(msyystring_buffer);
        label->numbindings++;
      }
#else
      msSetError(MS_IDENTERR, "Keyword FONT is not valid without TrueType font support.", "loadlabel()");    
      return(-1);
#endif
      break;
    case(FORCE):
      if((label->force = getSymbol(2, MS_TRUE,MS_FALSE)) == -1) return(-1);
      break;
    case(LABEL):
      break; /* for string loads */
    case(MAXSIZE):      
      if(getDouble(&(label->maxsize)) == -1) return(-1);
      break;
    case(MAXSCALEDENOM):
      if(getDouble(&(label->maxscaledenom)) == -1) return(-1);
      break;
    case(MAXLENGTH):
      if(getInteger(&(label->maxlength)) == -1) return(-1);
      break;
    case(MINLENGTH):
      if(getInteger(&(label->minlength)) == -1) return(-1);
      break; 
    case(MINDISTANCE):      
      if(getInteger(&(label->mindistance)) == -1) return(-1);
      break;
    case(REPEATDISTANCE):      
      if(getInteger(&(label->repeatdistance)) == -1) return(-1);
      break;
    case(MAXOVERLAPANGLE):      
      if(getDouble(&(label->maxoverlapangle)) == -1) return(-1);
      break;
    case(MINFEATURESIZE):
      if((symbol = getSymbol(2, MS_NUMBER,MS_AUTO)) == -1)  return(-1);
      if(symbol == MS_NUMBER)
	label->minfeaturesize = (int)msyynumber;
      else
	label->autominfeaturesize = MS_TRUE;
      break;
    case(MINSCALEDENOM):
      if(getDouble(&(label->minscaledenom)) == -1) return(-1);
      break;
    case(MINSIZE):
      if(getDouble(&(label->minsize)) == -1) return(-1);
      break;
    case(OFFSET):
      if(getInteger(&(label->offsetx)) == -1) return(-1);
      if(getInteger(&(label->offsety)) == -1) return(-1);
      break;
    case(OUTLINECOLOR):
      if(loadColor(&(label->outlinecolor), &(label->bindings[MS_LABEL_BINDING_OUTLINECOLOR])) != MS_SUCCESS) return(-1);      
      if(label->bindings[MS_LABEL_BINDING_OUTLINECOLOR].item) label->numbindings++;
      break;
    case(OUTLINEWIDTH):
      if(getInteger(&(label->outlinewidth)) == -1) return(-1);
      break;
    case(PARTIALS):
      if((label->partials = getSymbol(2, MS_TRUE,MS_FALSE)) == -1) return(-1);
      break;
    case(POSITION):
      if((label->position = getSymbol(11, MS_UL,MS_UC,MS_UR,MS_CL,MS_CC,MS_CR,MS_LL,MS_LC,MS_LR,MS_AUTO,MS_BINDING)) == -1)
        return(-1);
      if(label->position == MS_BINDING) {
        if(label->bindings[MS_LABEL_BINDING_POSITION].item != NULL)
	  msFree(label->bindings[MS_LABEL_BINDING_POSITION].item);
	  label->bindings[MS_LABEL_BINDING_POSITION].item = strdup(msyystring_buffer);
	  label->numbindings++;
	}
      break;
    case(PRIORITY):
      if((symbol = getSymbol(2, MS_NUMBER,MS_BINDING)) == -1) return(-1);
      if(symbol == MS_NUMBER) {
        label->priority = (int) msyynumber;
        if(label->priority < 1 || label->priority > MS_MAX_LABEL_PRIORITY) {
          msSetError(MS_MISCERR, "Invalid PRIORITY, must be an integer between 1 and %d." , "loadLabel()", MS_MAX_LABEL_PRIORITY);
          return(-1);
        }
      } else {
        if (label->bindings[MS_LABEL_BINDING_PRIORITY].item != NULL)
          msFree(label->bindings[MS_LABEL_BINDING_PRIORITY].item);
        label->bindings[MS_LABEL_BINDING_PRIORITY].item = msStrdup(msyystring_buffer);
        label->numbindings++;
      }
      break;
    case(SHADOWCOLOR):
      if(loadColor(&(label->shadowcolor), NULL) != MS_SUCCESS) return(-1);      
      break;
    case(SHADOWSIZE):
      /* if(getInteger(&(label->shadowsizex)) == -1) return(-1); */
      if((symbol = getSymbol(2, MS_NUMBER,MS_BINDING)) == -1) return(-1);
      if(symbol == MS_NUMBER) {
        label->shadowsizex = (int) msyynumber;
      } else {
        if (label->bindings[MS_LABEL_BINDING_SHADOWSIZEX].item != NULL)
          msFree(label->bindings[MS_LABEL_BINDING_SHADOWSIZEX].item);
        label->bindings[MS_LABEL_BINDING_SHADOWSIZEX].item = msStrdup(msyystring_buffer);
        label->numbindings++;
      }

      /* if(getInteger(&(label->shadowsizey)) == -1) return(-1); */
      if((symbol = getSymbol(2, MS_NUMBER,MS_BINDING)) == -1) return(-1);
      if(symbol == MS_NUMBER) {
	label->shadowsizey = (int) msyynumber;
      } else {
	if (label->bindings[MS_LABEL_BINDING_SHADOWSIZEY].item != NULL)
          msFree(label->bindings[MS_LABEL_BINDING_SHADOWSIZEY].item);
        label->bindings[MS_LABEL_BINDING_SHADOWSIZEY].item = msStrdup(msyystring_buffer);
        label->numbindings++;
      }
      break;
    case(SIZE):
#if defined (USE_GD_TTF) || defined (USE_GD_FT)
      if(label->bindings[MS_LABEL_BINDING_SIZE].item) {
        msFree(label->bindings[MS_LABEL_BINDING_SIZE].item);
        label->bindings[MS_LABEL_BINDING_SIZE].item = NULL;
	label->numbindings--;
      }

      if((symbol = getSymbol(7, MS_NUMBER,MS_BINDING,MS_TINY,MS_SMALL,MS_MEDIUM,MS_LARGE,MS_GIANT)) == -1) 
	return(-1);

      if(symbol == MS_NUMBER) {
        label->size = (double) msyynumber;
      } else if(symbol == MS_BINDING) {
        label->bindings[MS_LABEL_BINDING_SIZE].item = msStrdup(msyystring_buffer);
        label->numbindings++;
      } else
        label->size = symbol;
#else
      if((label->size = getSymbol(5, MS_TINY,MS_SMALL,MS_MEDIUM,MS_LARGE,MS_GIANT)) == -1) 
        return(-1);
#endif
      break; 
    case(STYLE):
      if(msGrowLabelStyles(label) == NULL)
        return(-1);
      initStyle(label->styles[label->numstyles]);
      if(loadStyle(label->styles[label->numstyles]) != MS_SUCCESS) return(-1);
      if(label->styles[label->numstyles]->_geomtransform.type == MS_GEOMTRANSFORM_NONE) 
        label->styles[label->numstyles]->_geomtransform.type = MS_GEOMTRANSFORM_LABELPOINT; /* set a default, a marker? */
      label->numstyles++;
      break;
    case(TYPE):
      if((label->type = getSymbol(2, MS_TRUETYPE,MS_BITMAP)) == -1) return(-1);
      break;    
    case(WRAP):      
      if(getCharacter(&(label->wrap)) == -1) return(-1);
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadLabel()", msyystring_buffer, msyylineno);
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  } /* next token */
}

int msUpdateLabelFromString(labelObj *label, char *string)
{
  if(!label || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadLabel(label) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

static void writeLabel(FILE *stream, int indent, labelObj *label)
{
  int i;

  if(label->size == -1) return; /* there is no default label anymore */

  indent++;
  writeBlockBegin(stream, indent, "LABEL");

  /*
  ** a few attributes are bitmap or truetype only
  */
  if(label->type == MS_BITMAP) {
    writeKeyword(stream, indent, "SIZE", (int)label->size, 5, MS_TINY, "TINY", MS_SMALL, "SMALL", MS_MEDIUM, "MEDIUM", MS_LARGE, "LARGE", MS_GIANT, "GIANT");
  } else {
    if(label->numbindings > 0 && label->bindings[MS_LABEL_BINDING_ANGLE].item)
      writeAttributeBinding(stream, indent, "ANGLE", &(label->bindings[MS_LABEL_BINDING_ANGLE]));
    else writeNumberOrKeyword(stream, indent, "ANGLE", 0, label->angle, label->anglemode, 3, MS_FOLLOW, "FOLLOW", MS_AUTO, "AUTO", MS_AUTO2, "AUTO2");

    writeKeyword(stream, indent, "ANTIALIAS", label->antialias, 1, MS_TRUE, "TRUE");

    if(label->numbindings > 0 && label->bindings[MS_LABEL_BINDING_FONT].item)
      writeAttributeBinding(stream, indent, "FONT", &(label->bindings[MS_LABEL_BINDING_FONT]));
    else writeString(stream, indent, "FONT", NULL, label->font);

    writeNumber(stream, indent, "MAXSIZE",  MS_MAXFONTSIZE, label->maxsize);
    writeNumber(stream, indent, "MINSIZE",  MS_MINFONTSIZE, label->minsize);

    if(label->numbindings > 0 && label->bindings[MS_LABEL_BINDING_SIZE].item)
      writeAttributeBinding(stream, indent, "SIZE", &(label->bindings[MS_LABEL_BINDING_SIZE]));
    else writeNumber(stream, indent, "SIZE", -1, label->size);
  }

  writeKeyword(stream, indent, "ALIGN", label->align, MS_ALIGN_CENTER, "CENTER", MS_ALIGN_RIGHT, "RIGHT");
  writeNumber(stream, indent, "BUFFER", 0, label->buffer);

  if(label->numbindings > 0 && label->bindings[MS_LABEL_BINDING_COLOR].item)
    writeAttributeBinding(stream, indent, "COLOR", &(label->bindings[MS_LABEL_BINDING_COLOR]));
  else writeColor(stream, indent, "COLOR", &(label->color));

  writeString(stream, indent, "ENCODING", NULL, label->encoding);
  writeKeyword(stream, indent, "FORCE", label->force, 1, MS_TRUE, "TRUE");
  writeNumber(stream, indent, "MAXLENGTH", 0, label->maxlength);
  writeNumber(stream, indent, "MAXSCALEDENOM", -1, label->maxscaledenom);
  writeNumber(stream, indent, "MINDISTANCE", -1, label->mindistance);
  writeNumberOrKeyword(stream, indent, "MINFEATURESIZE", -1, label->minfeaturesize, 1, label->autominfeaturesize, MS_TRUE, "AUTO");
  writeNumber(stream, indent, "MINLENGTH", 0, label->minlength);
  writeNumber(stream, indent, "MINSCALEDENOM", -1, label->minscaledenom);
  writeDimension(stream, indent, "OFFSET",  label->offsetx, label->offsety, NULL, NULL);
    
  if(label->numbindings > 0 && label->bindings[MS_LABEL_BINDING_OUTLINECOLOR].item)
    writeAttributeBinding(stream, indent, "OUTLINECOLOR", &(label->bindings[MS_LABEL_BINDING_OUTLINECOLOR]));
  else writeColor(stream, indent, "OUTLINECOLOR", &(label->outlinecolor));

  writeNumber(stream, indent, "OUTLINEWIDTH", 1, label->outlinewidth);
  writeKeyword(stream, indent, "PARTIALS", label->partials, 1, MS_FALSE, "FALSE");

  if(label->numbindings > 0 && label->bindings[MS_LABEL_BINDING_POSITION].item)
     writeAttributeBinding(stream, indent, "POSITION", &(label->bindings[MS_LABEL_BINDING_POSITION]));
  else writeKeyword(stream, indent, "POSITION", label->position, 10, MS_UL, "UL", MS_UC, "UC", MS_UR, "UR", MS_CL, "CL", MS_CC, "CC", MS_CR, "CR", MS_LL, "LL", MS_LC, "LC", MS_LR, "LR", MS_AUTO, "AUTO");

  if(label->numbindings > 0 && label->bindings[MS_LABEL_BINDING_PRIORITY].item)
    writeAttributeBinding(stream, indent, "PRIORITY", &(label->bindings[MS_LABEL_BINDING_PRIORITY]));
  else writeNumber(stream, indent, "PRIORITY", MS_DEFAULT_LABEL_PRIORITY, label->priority);

  writeNumber(stream, indent, "REPEATDISTANCE", 0, label->repeatdistance);
  writeColor(stream, indent, "SHADOWCOLOR", &(label->shadowcolor));
  writeDimension(stream, indent, "SHADOWSIZE", label->shadowsizex, label->shadowsizey, label->bindings[MS_LABEL_BINDING_SHADOWSIZEX].item, label->bindings[MS_LABEL_BINDING_SHADOWSIZEY].item);

  writeNumber(stream, indent, "MAXOVERLAPANGLE", 22.5, label->maxoverlapangle);
  for(i=0; i<label->numstyles; i++)
    writeStyle(stream, indent, label->styles[i]);
  writeKeyword(stream, indent, "TYPE", label->type, 2, MS_BITMAP, "BITMAP", MS_TRUETYPE, "TRUETYPE");
  writeCharacter(stream, indent, "WRAP", '\0', label->wrap);
  writeBlockEnd(stream, indent, "LABEL");
}

void initExpression(expressionObj *exp)
{
  exp->type = MS_STRING;
  exp->string = NULL;
  exp->compiled = MS_FALSE;
  exp->flags = 0;
  exp->tokens = exp->curtoken = NULL;
}

void freeExpressionTokens(expressionObj *exp)
{
  tokenListNodeObjPtr node = NULL;
  tokenListNodeObjPtr nextNode = NULL;

  if(!exp) return;

  if(exp->tokens) {
    node = exp->tokens;
    while (node != NULL) {
      nextNode = node->next;

      switch(node->token) {
      case MS_TOKEN_BINDING_DOUBLE:
      case MS_TOKEN_BINDING_INTEGER:
      case MS_TOKEN_BINDING_STRING:
      case MS_TOKEN_BINDING_TIME:
        msFree(node->tokenval.bindval.item);
        break;
      case MS_TOKEN_LITERAL_TIME:
        /* anything to do? */
        break;
      case MS_TOKEN_LITERAL_STRING:
        msFree(node->tokenval.strval);
        break;
      case MS_TOKEN_LITERAL_SHAPE:
        msFreeShape(node->tokenval.shpval);
        free(node->tokenval.shpval);
        break;
      }

      msFree(node);
      node = nextNode;
    }
    exp->tokens = exp->curtoken = NULL;
  }
}

void freeExpression(expressionObj *exp)
{
  if(!exp) return;
  msFree(exp->string);
  if((exp->type == MS_REGEX) && exp->compiled) ms_regfree(&(exp->regex));
  freeExpressionTokens(exp);
  initExpression(exp); /* re-initialize */
}

int loadExpression(expressionObj *exp)
{
  /* TODO: should we fall freeExpression if exp->string != NULL? We do some checking to avoid a leak but is it enough... */

  msyystring_icase = MS_TRUE;
  if((exp->type = getSymbol(5, MS_STRING,MS_EXPRESSION,MS_REGEX,MS_ISTRING,MS_IREGEX)) == -1) return(-1);
  if (exp->string != NULL)
    msFree(exp->string);
  exp->string = msStrdup(msyystring_buffer);

  if(exp->type == MS_ISTRING) {
    exp->flags = exp->flags | MS_EXP_INSENSITIVE;
    exp->type = MS_STRING;
  } else if(exp->type == MS_IREGEX) {
    exp->flags = exp->flags | MS_EXP_INSENSITIVE;
    exp->type = MS_REGEX;
  }
  
  return(0);
}

/* ---------------------------------------------------------------------------
   msLoadExpressionString and loadExpressionString

   msLoadExpressionString wraps call to loadExpressionString with mutex
   acquisition and release.  This function should be used everywhere outside
   the mapfile loading phase of an application.  loadExpressionString does
   not check for a mutex!  It should be used only within code that has
   properly acquired a mutex.

   See bug 339 for more details -- SG.
   ------------------------------------------------------------------------ */
   
int msLoadExpressionString(expressionObj *exp, char *value)
{
  int retval = MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );
  retval = loadExpressionString( exp, value );
  msReleaseLock( TLOCK_PARSER );

  return retval;
}

int loadExpressionString(expressionObj *exp, char *value)
{
  msyystate = MS_TOKENIZE_STRING; msyystring = value;
  msyylex(); /* sets things up but processes no tokens */

  freeExpression(exp); /* we're totally replacing the old expression so free (which re-inits) to start over */

  msyystring_icase = MS_TRUE;
  if((exp->type = getSymbol(4, MS_EXPRESSION,MS_REGEX,MS_IREGEX,MS_ISTRING)) != -1) {
    exp->string = msStrdup(msyystring_buffer);

    if(exp->type == MS_ISTRING) {
      exp->type = MS_STRING;
      exp->flags = exp->flags | MS_EXP_INSENSITIVE;
    } else if(exp->type == MS_IREGEX) {
      exp->type = MS_REGEX;
      exp->flags = exp->flags | MS_EXP_INSENSITIVE;
    }
  } else {
    msResetErrorList(); /* failure above is not really an error since we'll consider anything not matching (like an unquoted number) as a STRING) */
    exp->type = MS_STRING;
    if((strlen(value) - strlen(msyystring_buffer)) == 2)
      exp->string = msStrdup(msyystring_buffer); /* value was quoted */
    else
      exp->string = msStrdup(value); /* use the whole value */
  }

  return(0); 
}

/* msGetExpressionString()
 *
 * Returns the string representation of this expression, including delimiters
 * and any flags (e.g. i = case-insensitive).
 *
 * Returns a newly allocated buffer that should be freed by the caller or NULL.
 */
char *msGetExpressionString(expressionObj *exp) 
{
  if(exp->string) {
    char *exprstring;
    size_t buffer_size;
    const char *case_insensitive = "";

    if(exp->flags & MS_EXP_INSENSITIVE)
      case_insensitive = "i";

    /* Alloc buffer big enough for string + 2 delimiters + 'i' + \0 */
    buffer_size = strlen(exp->string)+4;
    exprstring = (char*)msSmallMalloc(buffer_size);

    switch(exp->type) {
    case(MS_REGEX):
      snprintf(exprstring, buffer_size, "/%s/%s", exp->string, case_insensitive);
      return exprstring;
    case(MS_STRING):
      snprintf(exprstring, buffer_size, "\"%s\"%s", exp->string, case_insensitive);
      return exprstring;
    case(MS_EXPRESSION):
      snprintf(exprstring, buffer_size, "(%s)", exp->string);
      return exprstring;
    default:
      /* We should never get to here really! */
      free(exprstring);
      return NULL;
    }
  }
  return NULL;
}

static void writeExpression(FILE *stream, int indent, const char *name, expressionObj *exp)
{
  char *string_tmp;
  if(!exp || !exp->string) return;

  writeIndent(stream, ++indent);
  switch(exp->type) {
  case(MS_REGEX):
    fprintf(stream, "%s /%s/", name, exp->string);
    break;
  case(MS_STRING):
      if ( (strchr(exp->string, '\'') == NULL) && (strchr(exp->string, '\"') == NULL))
          fprintf(stream, "%s \"%s\"", name, exp->string);
      else if ( (strchr(exp->string, '\"') != NULL) && (strchr(exp->string, '\'') == NULL))
          fprintf(stream, "%s \'%s\'", name, exp->string);
      else if ( (strchr(exp->string, '\'') != NULL) && (strchr(exp->string, '\"') == NULL))
          fprintf(stream, "%s \"%s\"", name, exp->string);
      else {
          string_tmp = msStringEscape(exp->string);
          fprintf(stream, "%s \"%s\"", name, string_tmp);
          free(string_tmp);
      }   
    break;
  case(MS_EXPRESSION):
    fprintf(stream, "%s (%s)", name, exp->string);
    break;
  }
  if((exp->type == MS_STRING || exp->type == MS_REGEX) && (exp->flags & MS_EXP_INSENSITIVE))
    fprintf(stream, "i");
  writeLineFeed(stream);
}

int loadHashTable(hashTableObj *ptable)
{
  char *key=NULL, *data=NULL;
  
  if (!ptable) ptable = msCreateHashTable();

  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadHashTable()");
      return(MS_FAILURE);
    case(END):
      return(MS_SUCCESS);
    case(MS_STRING):
      key = msStrdup(msyystring_buffer); /* the key is *always* a string */
      if(getString(&data) == MS_FAILURE) return(MS_FAILURE);      
      msInsertHashTable(ptable, key, data);      

      free(key);
      free(data); data=NULL;
      break;
    default:
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadHashTable()", msyystring_buffer, msyylineno );
      return(MS_FAILURE);
    }
  }

  return(MS_SUCCESS);
}

static void writeHashTable(FILE *stream, int indent, const char *title, hashTableObj *table) {
  struct hashObj *tp;
  int i;

  if(!table) return;
  if(msHashIsEmpty(table)) return;

  indent++;
  writeBlockBegin(stream, indent, title);
  for (i=0;i<MS_HASHSIZE; i++) {
    if (table->items[i] != NULL) {
      for (tp=table->items[i]; tp!=NULL; tp=tp->next)
        writeNameValuePair(stream, indent, tp->key, tp->data);
    }
  }
  writeBlockEnd(stream, indent, title);
}

static void writeHashTableInline(FILE *stream, int indent, char *name, hashTableObj* table) {
    struct hashObj *tp = NULL;
    int i;

    if(!table) return;
    if(msHashIsEmpty(table)) return;

    ++indent;
    for (i=0;i<MS_HASHSIZE;++i) {
        if (table->items[i] != NULL) {
            for (tp=table->items[i]; tp!=NULL; tp=tp->next) {
                writeIndent(stream, indent);
                fprintf(stream, "%s \"%s\" \"%s\"\n", name, tp->key, tp->data);
            }
        }
    }
}

/*
** Initialize, load and free a cluster object
*/
void initCluster(clusterObj *cluster) {
    cluster->maxdistance = 10;
    cluster->buffer = 0;
    cluster->region = NULL;
    initExpression(&(cluster->group));
    initExpression(&(cluster->filter));
}

void freeCluster(clusterObj *cluster) {
    msFree(cluster->region);
    freeExpression(&(cluster->group));
    freeExpression(&(cluster->filter));
}

int loadCluster(clusterObj *cluster) {
  for(;;) {
    switch(msyylex()) {
    case(CLUSTER):
      break; /* for string loads */
    case(MAXDISTANCE):
      if(getDouble(&(cluster->maxdistance)) == -1) return(-1);
      break;
    case(BUFFER):
      if(getDouble(&(cluster->buffer)) == -1) return(-1);
      break;
    case(REGION):
      if(getString(&cluster->region) == MS_FAILURE) return(-1);
      break;
    case(END):
      return(0);
      break;
    case(GROUP):
      if(loadExpression(&(cluster->group)) == -1) return(-1);
      break;
    case(FILTER):
      if(loadExpression(&(cluster->filter)) == -1) return(-1);
      break;
    }
  }
  return(MS_SUCCESS);
}

int msUpdateClusterFromString(clusterObj *cluster, char *string)
{
  if(!cluster || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadCluster(cluster) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

static void writeCluster(FILE *stream, int indent, clusterObj *cluster) {
  indent++;
  writeBlockBegin(stream, indent, "CLUSTER");
  writeNumber(stream, indent, "MAXDISTANCE", 0, cluster->maxdistance);
  writeNumber(stream, indent, "BUFFER", 0, cluster->maxdistance);
  writeString(stream, indent, "REGION", NULL, cluster->region);
  writeExpression(stream, indent, "GROUP", &(cluster->group));
  writeExpression(stream, indent, "FILTER", &(cluster->filter));
  writeBlockEnd(stream, indent, "CLUSTER");
}

/*
** Initialize, load and free a single style
*/
int initStyle(styleObj *style) {
  int i;
  MS_REFCNT_INIT(style);
  MS_INIT_COLOR(style->color, -1,-1,-1,255); /* must explictly set colors */
  MS_INIT_COLOR(style->backgroundcolor, -1,-1,-1,255);
  MS_INIT_COLOR(style->outlinecolor, -1,-1,-1,255);
  /* New Color Range fields*/
  MS_INIT_COLOR(style->mincolor, -1,-1,-1,255);
  MS_INIT_COLOR(style->maxcolor, -1,-1,-1,255);
  style->minvalue = 0.0;
  style->maxvalue = 1.0;
  style->rangeitem = NULL;
  /* End Color Range fields*/
  style->symbol = 0; /* there is always a default symbol*/
  style->symbolname = NULL;
  style->size = -1; /* in SIZEUNITS (layerObj) */
  style->minsize = MS_MINSYMBOLSIZE;
  style->maxsize = MS_MAXSYMBOLSIZE;
  style->width = 1; /* in pixels */
  style->outlinewidth = 0; /* in pixels */
  style->minwidth = MS_MINSYMBOLWIDTH;
  style->maxwidth = MS_MAXSYMBOLWIDTH;
  style->minscaledenom=style->maxscaledenom = -1.0;
  style->offsetx = style->offsety = 0; /* no offset */
  style->antialias = MS_FALSE;
  style->angle = 0;
  style->autoangle= MS_FALSE;
  style->opacity = 100; /* fully opaque */

  initExpression(&(style->_geomtransform));
  style->_geomtransform.type = MS_GEOMTRANSFORM_NONE;

  style->patternlength = 0; /* solid line */
  style->gap = 0;
  style->position = MS_CC;
  style->linecap = MS_CJC_ROUND;
  style->linejoin = MS_CJC_NONE;
  style->linejoinmaxsize = 3;

  style->numbindings = 0;
  for(i=0; i<MS_STYLE_BINDING_LENGTH; i++) {
    style->bindings[i].item = NULL;
    style->bindings[i].index = -1;
  }

  return MS_SUCCESS;
}

int loadStyle(styleObj *style) {
  int symbol;

  for(;;) {
    switch(msyylex()) {
  /* New Color Range fields*/
    case (COLORRANGE):
      /*These are both in one line now*/
      if(loadColor(&(style->mincolor), NULL) != MS_SUCCESS) return(MS_FAILURE);
      if(loadColor(&(style->maxcolor), NULL) != MS_SUCCESS) return(MS_FAILURE);
      break;
    case(DATARANGE):
      /*These are both in one line now*/
      if(getDouble(&(style->minvalue)) == -1) return(-1);
      if(getDouble(&(style->maxvalue)) == -1) return(-1);
      break;
    case(RANGEITEM):
      if(getString(&style->rangeitem) == MS_FAILURE) return(-1);
      break;
  /* End Range fields*/
    case(ANGLE):
      if((symbol = getSymbol(3, MS_NUMBER,MS_BINDING,MS_AUTO)) == -1) return(MS_FAILURE);

      if(symbol == MS_NUMBER)
        style->angle = (double) msyynumber;
      else if(symbol==MS_BINDING){
        if (style->bindings[MS_STYLE_BINDING_ANGLE].item != NULL)
          msFree(style->bindings[MS_STYLE_BINDING_ANGLE].item);
        style->bindings[MS_STYLE_BINDING_ANGLE].item = msStrdup(msyystring_buffer);
        style->numbindings++;
      } else {
        style->autoangle=MS_TRUE;
      }
      break;
    case(ANTIALIAS):
      if((style->antialias = getSymbol(2, MS_TRUE,MS_FALSE)) == -1)
	return(MS_FAILURE);
      break;
    case(BACKGROUNDCOLOR):
      if(loadColor(&(style->backgroundcolor), NULL) != MS_SUCCESS) return(MS_FAILURE);
      break;
    case(COLOR):
      if(loadColor(&(style->color), &(style->bindings[MS_STYLE_BINDING_COLOR])) != MS_SUCCESS) return(MS_FAILURE);
      if(style->bindings[MS_STYLE_BINDING_COLOR].item) style->numbindings++;
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadStyle()");
      return(MS_FAILURE); /* missing END (probably) */
    case(END): 
      {
        int alpha;

        /* apply opacity as the alpha channel color(s) */
        if(style->opacity < 100) {
          alpha = MS_NINT(style->opacity*2.55);

          style->color.alpha = alpha; 
          style->outlinecolor.alpha = alpha;
          style->backgroundcolor.alpha = alpha;

          style->mincolor.alpha = alpha;
          style->maxcolor.alpha = alpha;
        }

        return(MS_SUCCESS);
      }
      break;
    case(GAP):
      if((getDouble(&style->gap)) == -1) return(MS_FAILURE);
      break;
    case(MAXSCALEDENOM):
      if(getDouble(&(style->maxscaledenom)) == -1) return(MS_FAILURE);
      break;
    case(MINSCALEDENOM):
      if(getDouble(&(style->minscaledenom)) == -1) return(MS_FAILURE);
      break;
    case(GEOMTRANSFORM):
      {
        int s;
	if((s = getSymbol(2, MS_STRING, MS_EXPRESSION)) == -1) return(MS_FAILURE);
        if(s == MS_STRING)
          msStyleSetGeomTransform(style, msyystring_buffer);
        else {
          /* handle expression case here for the moment */
          msFree(style->_geomtransform.string);
	  style->_geomtransform.string = msStrdup(msyystring_buffer);
          style->_geomtransform.type = MS_GEOMTRANSFORM_EXPRESSION;
        }
      }
      break;
    case(LINECAP):
      if((style->linecap = getSymbol(4,MS_CJC_BUTT, MS_CJC_ROUND, MS_CJC_SQUARE, MS_CJC_TRIANGLE)) == -1) return(MS_FAILURE);
      break;
    case(LINEJOIN):
      if((style->linejoin = getSymbol(4,MS_CJC_NONE, MS_CJC_ROUND, MS_CJC_MITER, MS_CJC_BEVEL)) == -1) return(MS_FAILURE);
      break;
    case(LINEJOINMAXSIZE):
      if((getDouble(&style->linejoinmaxsize)) == -1) return(MS_FAILURE);
      break;
    case(MAXSIZE):
      if(getDouble(&(style->maxsize)) == -1) return(MS_FAILURE);      
      break;
    case(MINSIZE):
      if(getDouble(&(style->minsize)) == -1) return(MS_FAILURE);
      break;
    case(MAXWIDTH):
      if(getDouble(&(style->maxwidth)) == -1) return(MS_FAILURE);
      break;
    case(MINWIDTH):
      if(getDouble(&(style->minwidth)) == -1) return(MS_FAILURE);
      break;
    case(OFFSET):
      if(getDouble(&(style->offsetx)) == -1) return(MS_FAILURE);
      if(getDouble(&(style->offsety)) == -1) return(MS_FAILURE);
      break;
    case(OPACITY):
      if((symbol = getSymbol(2, MS_NUMBER,MS_BINDING)) == -1) return(MS_FAILURE);
      if(symbol == MS_NUMBER)
        style->opacity = MS_MAX(MS_MIN((int) msyynumber, 100), 0); /* force opacity to between 0 and 100 */
      else {
        if (style->bindings[MS_STYLE_BINDING_OPACITY].item != NULL)
          msFree(style->bindings[MS_STYLE_BINDING_OPACITY].item);
        style->bindings[MS_STYLE_BINDING_OPACITY].item = msStrdup(msyystring_buffer);
        style->numbindings++;
      }
      break;
    case(OUTLINECOLOR):
      if(loadColor(&(style->outlinecolor), &(style->bindings[MS_STYLE_BINDING_OUTLINECOLOR])) != MS_SUCCESS) return(MS_FAILURE);
      if(style->bindings[MS_STYLE_BINDING_OUTLINECOLOR].item) style->numbindings++;
      break;
    case(PATTERN): {
      int done = MS_FALSE;
      for(;;) { /* read till the next END */
        switch(msyylex()) {  
        case(END):
          if(style->patternlength < 2) {
            msSetError(MS_SYMERR, "Not enough pattern elements. A minimum of 2 are required", "loadStyle()");
            return(-1);
          }   
          done = MS_TRUE;
          break;
        case(MS_NUMBER): /* read the pattern values */
          if(style->patternlength == MS_MAXPATTERNLENGTH) {
            msSetError(MS_SYMERR, "Pattern too long.", "loadStyle()");
            return(-1);
          }
          style->pattern[style->patternlength] = atof(msyystring_buffer);
          style->patternlength++;
          break;
        default:
          msSetError(MS_TYPEERR, "Parsing error near (%s):(line %d)", "loadStyle()", msyystring_buffer, msyylineno);
          return(-1);
        }
        if(done == MS_TRUE)
          break;
      }      
      break;
    }
    case(POSITION):
      /* if((s->position = getSymbol(3, MS_UC,MS_CC,MS_LC)) == -1)  */
      /* return(-1); */
      if((style->position = getSymbol(9, MS_UL,MS_UC,MS_UR,MS_CL,MS_CC,MS_CR,MS_LL,MS_LC,MS_LR)) == -1) 
        return(-1);
      break;
    case(OUTLINEWIDTH):
      if((symbol = getSymbol(2, MS_NUMBER,MS_BINDING)) == -1) return(MS_FAILURE);
      if(symbol == MS_NUMBER) {
        style->outlinewidth = (double) msyynumber;
        if(style->outlinewidth < 0) {
          msSetError(MS_MISCERR, "Invalid OUTLINEWIDTH, must be greater than 0" , "loadStyle()");
          return(MS_FAILURE);
        }
      }
      else {
        if (style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].item != NULL)
          msFree(style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].item);
        style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].item = msStrdup(msyystring_buffer);
        style->numbindings++;
      }
      break;
    case(SIZE):
      if((symbol = getSymbol(2, MS_NUMBER,MS_BINDING)) == -1) return(MS_FAILURE);
      if(symbol == MS_NUMBER)
        style->size = (double) msyynumber;
      else {
        if (style->bindings[MS_STYLE_BINDING_SIZE].item != NULL)
          msFree(style->bindings[MS_STYLE_BINDING_SIZE].item);
 	style->bindings[MS_STYLE_BINDING_SIZE].item = msStrdup(msyystring_buffer);
        style->numbindings++;
      }
      break;
    case(STYLE):
      break; /* for string loads */
    case(SYMBOL):
      if((symbol = getSymbol(3, MS_NUMBER,MS_STRING,MS_BINDING)) == -1) return(MS_FAILURE);
      if(symbol == MS_NUMBER) {
        if (style->symbolname != NULL) {
          msFree(style->symbolname);
          style->symbolname = NULL;
        }
	style->symbol = (int) msyynumber;
      }
      else if(symbol == MS_STRING)
      {
        if (style->symbolname != NULL)
          msFree(style->symbolname);
        style->symbolname = msStrdup(msyystring_buffer);
      }
      else {
        if (style->bindings[MS_STYLE_BINDING_SYMBOL].item != NULL)
          msFree(style->bindings[MS_STYLE_BINDING_SYMBOL].item);
        style->bindings[MS_STYLE_BINDING_SYMBOL].item = msStrdup(msyystring_buffer);
        style->numbindings++;
      }
      break;
    case(WIDTH):
      if((symbol = getSymbol(2, MS_NUMBER,MS_BINDING)) == -1) return(MS_FAILURE);
      if(symbol == MS_NUMBER)
        style->width = (double) msyynumber;
      else {
        if (style->bindings[MS_STYLE_BINDING_WIDTH].item != NULL)
          msFree(style->bindings[MS_STYLE_BINDING_WIDTH].item);
        style->bindings[MS_STYLE_BINDING_WIDTH].item = msStrdup(msyystring_buffer);
        style->numbindings++;
      }
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadStyle()", msyystring_buffer, msyylineno);
        return(MS_FAILURE);
      } else {
        return(MS_SUCCESS); /* end of a string, not an error */
      }
    }
  }
}

int msUpdateStyleFromString(styleObj *style, char *string, int url_string)
{
  if(!style || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadStyle(style) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

int freeStyle(styleObj *style) {
  int i;

  if( MS_REFCNT_DECR_IS_NOT_ZERO(style) ) { return MS_FAILURE; }

  msFree(style->symbolname);
  freeExpression(&style->_geomtransform);
  msFree(style->rangeitem);

  for(i=0; i<MS_STYLE_BINDING_LENGTH; i++)
    msFree(style->bindings[i].item);

  return MS_SUCCESS;
}

void writeStyle(FILE *stream, int indent, styleObj *style) {

  indent++;
  writeBlockBegin(stream, indent, "STYLE");

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_ANGLE].item)
    writeAttributeBinding(stream, indent, "ANGLE", &(style->bindings[MS_STYLE_BINDING_ANGLE]));
  else writeNumberOrKeyword(stream, indent, "ANGLE", 360, style->angle, style->autoangle, 1, MS_TRUE, "AUTO");

  writeKeyword(stream, indent, "ANTIALIAS", style->antialias, 1, MS_TRUE, "TRUE");
  writeColor(stream, indent, "BACKGROUNDCOLOR", &(style->backgroundcolor));

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_COLOR].item)
    writeAttributeBinding(stream, indent, "COLOR", &(style->bindings[MS_STYLE_BINDING_COLOR]));
  else writeColor(stream, indent, "COLOR", &(style->color));

  writeNumber(stream, indent, "MAXSCALEDENOM", -1, style->maxscaledenom);
  writeNumber(stream, indent, "MAXSIZE", MS_MAXSYMBOLSIZE, style->maxsize);
  writeNumber(stream, indent, "MAXWIDTH", MS_MAXSYMBOLWIDTH, style->maxwidth);
  writeNumber(stream, indent, "MINSCALEDENOM", -1, style->minscaledenom);
  writeNumber(stream, indent, "MINSIZE", MS_MINSYMBOLSIZE, style->minsize);
  writeNumber(stream, indent, "MINWIDTH", MS_MINSYMBOLWIDTH, style->minwidth);
  writeDimension(stream, indent, "OFFSET", style->offsetx, style->offsety, NULL, NULL);

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_OPACITY].item)
    writeAttributeBinding(stream, indent, "OPACITY", &(style->bindings[MS_STYLE_BINDING_OPACITY]));
  else writeNumber(stream, indent, "OPACITY", 100, style->opacity);

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_OUTLINECOLOR].item)
    writeAttributeBinding(stream, indent, "OUTLINECOLOR", &(style->bindings[MS_STYLE_BINDING_OUTLINECOLOR]));
  else writeColor(stream, indent, "OUTLINECOLOR", &(style->outlinecolor)); 

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH].item)
    writeAttributeBinding(stream, indent, "OUTLINEWIDTH", &(style->bindings[MS_STYLE_BINDING_OUTLINEWIDTH]));
  else writeNumber(stream, indent, "OUTLINEWIDTH", 0, style->outlinewidth);

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_SIZE].item)
    writeAttributeBinding(stream, indent, "SIZE", &(style->bindings[MS_STYLE_BINDING_SIZE]));
  else writeNumber(stream, indent, "SIZE", -1, style->size);

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_SYMBOL].item)
    writeAttributeBinding(stream, indent, "SYMBOL", &(style->bindings[MS_STYLE_BINDING_SYMBOL]));
  else writeNumberOrString(stream, indent, "SYMBOL", 0, style->symbol, style->symbolname);

  if(style->numbindings > 0 && style->bindings[MS_STYLE_BINDING_WIDTH].item)
    writeAttributeBinding(stream, indent, "WIDTH", &(style->bindings[MS_STYLE_BINDING_WIDTH]));
  else writeNumber(stream, indent, "WIDTH", 1, style->width);

  if(style->rangeitem) {
    writeString(stream, indent, "RANGEITEM", NULL, style->rangeitem);
    writeColorRange(stream, indent, "COLORRANGE", &(style->mincolor), &(style->maxcolor));
    writeDimension(stream, indent, "DATARANGE", style->minvalue, style->maxvalue, NULL, NULL);
  }

  if(style->_geomtransform.type != MS_GEOMTRANSFORM_NONE) {
    /*
    ** TODO: fprintf(stream, "        GEOMTRANSFORM \"%s\"\n",style->_geomtransformexpression);
    */
  }
  writeBlockEnd(stream, indent, "STYLE");
}

/*
** Initialize, load and free a single class
*/
int initClass(classObj *class)
{
  /* printf("Init class at %p\n", class); */

  class->status = MS_ON;
  class->debug = MS_OFF;
  MS_REFCNT_INIT(class);

  initExpression(&(class->expression));
  class->name = NULL;
  class->title = NULL;
  initExpression(&(class->text));
  
  initLabel(&(class->label));
  class->label.size = -1; /* no default */

  class->template = NULL;

  class->type = -1;
  
  initHashTable(&(class->metadata));
  initHashTable(&(class->validation));
  
  class->maxscaledenom = class->minscaledenom = -1.0;
  class->minfeaturesize = -1; /* no limit */

  /* Set maxstyles = 0, styles[] will be allocated as needed on first call 
   * to msGrowClassStyles()
   */
  class->numstyles = class->maxstyles = 0;   
  class->styles = NULL;  

  class->keyimage = NULL;

  class->group = NULL;

  return(0);
}

int freeClass(classObj *class)
{
  int i;

  if( MS_REFCNT_DECR_IS_NOT_ZERO(class) ) { return MS_FAILURE; }
  /* printf("Freeing class at %p (%s)\n", class, class->name); */

  freeLabel(&(class->label));
  freeExpression(&(class->expression));
  freeExpression(&(class->text));
  msFree(class->name);
  msFree(class->title);
  msFree(class->template);
  msFree(class->group);
  
  if (&(class->metadata)) msFreeHashItems(&(class->metadata));
  if (&(class->validation)) msFreeHashItems(&(class->validation));
  
  for(i=0;i<class->numstyles;i++) { /* each style */
    if(class->styles[i]!=NULL) {
      if(freeStyle(class->styles[i]) == MS_SUCCESS) {
        msFree(class->styles[i]);
      }
    }
  }
  msFree(class->styles);
  msFree(class->keyimage);

  return MS_SUCCESS;
}

/*
** Ensure there is at least one free entry in the sttyles array of this 
** classObj. Grow the allocated styles array if necessary and allocate 
** a new style for styles[numstyles] if there is not already one, 
** setting its contents to all zero bytes (i.e. does not call initStyle()
** on it).
**
** This function is safe to use for the initial allocation of the styles[]
** array as well (i.e. when maxstyles==0 and styles==NULL)
**
** Returns a reference to the new styleObj on success, NULL on error.
*/
styleObj *msGrowClassStyles( classObj *class )
{
    /* Do we need to increase the size of styles[] by  MS_STYLE_ALLOCSIZE?
     */
    if (class->numstyles == class->maxstyles) {
        styleObj **newStylePtr;
        int i, newsize;

        newsize = class->maxstyles + MS_STYLE_ALLOCSIZE;

        /* Alloc/realloc styles */
        newStylePtr = (styleObj**)realloc(class->styles,
                                          newsize*sizeof(styleObj*));
        MS_CHECK_ALLOC(newStylePtr, newsize*sizeof(styleObj*), NULL);

        class->styles = newStylePtr;
        class->maxstyles = newsize;
        for(i=class->numstyles; i<class->maxstyles; i++) {
            class->styles[i] = NULL;
        }
    }

    if (class->styles[class->numstyles]==NULL) {
        class->styles[class->numstyles]=(styleObj*)calloc(1,sizeof(styleObj));
        MS_CHECK_ALLOC(class->styles[class->numstyles], sizeof(styleObj), NULL);
    }

    return class->styles[class->numstyles];
}

/* exactly the same as for a classObj */
styleObj *msGrowLabelStyles( labelObj *label )
{
    /* Do we need to increase the size of styles[] by  MS_STYLE_ALLOCSIZE?
     */
    if (label->numstyles == label->maxstyles) {
        styleObj **newStylePtr;
        int i, newsize;

        newsize = label->maxstyles + MS_STYLE_ALLOCSIZE;

        /* Alloc/realloc styles */
        newStylePtr = (styleObj**)realloc(label->styles,
                                          newsize*sizeof(styleObj*));
        MS_CHECK_ALLOC(newStylePtr, newsize*sizeof(styleObj*), NULL);

        label->styles = newStylePtr;
        label->maxstyles = newsize;
        for(i=label->numstyles; i<label->maxstyles; i++) {
            label->styles[i] = NULL;
        }
    }

    if (label->styles[label->numstyles]==NULL) {
        label->styles[label->numstyles]=(styleObj*)calloc(1,sizeof(styleObj));
        MS_CHECK_ALLOC(label->styles[label->numstyles], sizeof(styleObj), NULL);
    }

    return label->styles[label->numstyles];
}

/* msMaybeAllocateClassStyle()
**
** Ensure that requested style index exists and has been initialized.
**
** Returns MS_SUCCESS/MS_FAILURE.
*/
int msMaybeAllocateClassStyle(classObj* c, int idx) {
    if (c==NULL) return MS_FAILURE;

    if ( idx < 0 ) {
        msSetError(MS_MISCERR, "Invalid style index: %d", "msMaybeAllocateClassStyle()", idx);
        return MS_FAILURE;
    }

    /* Alloc empty styles as needed up to idx.
     * Nothing to do if requested style already exists
     */
    while(c->numstyles <= idx) {
        if (msGrowClassStyles(c) == NULL)
            return MS_FAILURE;

        if ( initStyle(c->styles[c->numstyles]) == MS_FAILURE ) {
            msSetError(MS_MISCERR, "Failed to init new styleObj", 
                       "msMaybeAllocateClassStyle()");
            return(MS_FAILURE);
        }
        c->numstyles++;
    }
    return MS_SUCCESS;
}



/*
 * Reset style info in the class to defaults
 * the only members we don't touch are name, expression, and join/query stuff
 * This is used with STYLEITEM before overwriting the contents of a class.
 */
void resetClassStyle(classObj *class)
{
  int i;

  freeLabel(&(class->label));

  freeExpression(&(class->text));
  initExpression(&(class->text));

  /* reset styles */
  for(i=0; i<class->numstyles; i++) {
    if(class->styles[i] != NULL) {
      if( freeStyle(class->styles[i]) == MS_SUCCESS ) {
        msFree(class->styles[i]);
      }
      class->styles[i] = NULL;
    }
  }
  class->numstyles = 0;

  initLabel(&(class->label));
  class->label.size = -1; /* no default */

  class->type = -1;
  class->layer = NULL;
}

int loadClass(classObj *class, layerObj *layer)
{
  int state;
  mapObj *map=NULL;

  class->layer = (layerObj *) layer;
  if(layer && layer->map) map = layer->map;

  for(;;) {
    switch(msyylex()) {
    case(CLASS):
      break; /* for string loads */
    case(DEBUG):
      if((class->debug = getSymbol(3, MS_ON,MS_OFF, MS_NUMBER)) == -1) return(-1);
      if(class->debug == MS_NUMBER) class->debug = (int) msyynumber;
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadClass()");
      return(-1);
    case(END):
      return(0);
      break;
    case(EXPRESSION):
      if(loadExpression(&(class->expression)) == -1) return(-1); /* loadExpression() cleans up previously allocated expression */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(class->expression.string, msLookupHashTable(&(class->validation), "expression"), msLookupHashTable(&(layer->validation), "expression"), msLookupHashTable(&(map->web.validation), "expression"), NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based EXPRESSION configuration failed pattern validation." , "loadClass()");
          freeExpression(&(class->expression));
          return(-1);
        }
      }
      break;
    case(GROUP):
      if(getString(&class->group) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(class->group, msLookupHashTable(&(class->validation), "group"), msLookupHashTable(&(layer->validation), "group"), msLookupHashTable(&(map->web.validation), "group"), NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based GROUP configuration failed pattern validation." , "loadClass()");
          msFree(class->group); class->group=NULL;
          return(-1);
        }
      }
      break;      
    case(KEYIMAGE):
      if(getString(&class->keyimage) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(class->keyimage, msLookupHashTable(&(class->validation), "keyimage"), msLookupHashTable(&(layer->validation), "keyimage"), msLookupHashTable(&(map->web.validation), "keyimage"), NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based KEYIMAGE configuration failed pattern validation." , "loadClass()");
          msFree(class->keyimage); class->keyimage=NULL;
          return(-1);
        }
      }
      break;
    case(LABEL):
      class->label.size = MS_MEDIUM; /* only set a default if the LABEL section is present */
      if(loadLabel(&(class->label)) == -1) return(-1);
      break;
    case(MAXSCALE):
    case(MAXSCALEDENOM):
      if(getDouble(&(class->maxscaledenom)) == -1) return(-1);
      break;
    case(METADATA):
      if(loadHashTable(&(class->metadata)) != MS_SUCCESS) return(-1);
      break;
    case(MINSCALE):
    case(MINSCALEDENOM):
      if(getDouble(&(class->minscaledenom)) == -1) return(-1);
      break;
    case(MINFEATURESIZE):
      if(getInteger(&(class->minfeaturesize)) == -1) return(-1);
      break;
    case(NAME):
      if(getString(&class->name) == MS_FAILURE) return(-1);
      break;         
    case(STATUS):
      if((class->status = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;    
    case(STYLE):
      if(msGrowClassStyles(class) == NULL)
        return(-1);
      initStyle(class->styles[class->numstyles]);
      if(loadStyle(class->styles[class->numstyles]) != MS_SUCCESS) return(-1);
      class->numstyles++;
      break;
    case(TEMPLATE):
      if(getString(&class->template) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(class->template, msLookupHashTable(&(class->validation), "template"), msLookupHashTable(&(layer->validation), "template"), msLookupHashTable(&(map->web.validation), "template"), map->templatepattern) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based TEMPLATE configuration failed pattern validation." , "loadClass()");
          msFree(class->template); class->template=NULL;
          return(-1);
	}
      }
      break;
    case(TEXT):
      if(loadExpression(&(class->text)) == -1) return(-1); /* loadExpression() cleans up previously allocated expression */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(class->text.string, msLookupHashTable(&(class->validation), "text"), msLookupHashTable(&(layer->validation), "text"), msLookupHashTable(&(map->web.validation), "text"), NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based TEXT configuration failed pattern validation." , "loadClass()");
          freeExpression(&(class->text));
          return(-1);
        }
      }
      if((class->text.type != MS_STRING) && (class->text.type != MS_EXPRESSION)) {
	msSetError(MS_MISCERR, "Text expressions support constant or tagged replacement strings." , "loadClass()");
	return(-1);
      }
      break;
    case(TITLE):
      if(getString(&class->title) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(class->title, msLookupHashTable(&(class->validation), "title"), msLookupHashTable(&(layer->validation), "title"), msLookupHashTable(&(map->web.validation), "title"), NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based TITLE configuration failed pattern validation." , "loadClass()");
          msFree(class->title); class->title=NULL;
          return(-1);
        }
      }
      break;
    case(TYPE):
      if((class->type = getSymbol(6, MS_LAYER_POINT,MS_LAYER_LINE,MS_LAYER_RASTER,MS_LAYER_POLYGON,MS_LAYER_ANNOTATION,MS_LAYER_CIRCLE)) == -1) return(-1);
      break;

    /*
    ** for backwards compatability, these are shortcuts for style 0
    */
    case(BACKGROUNDCOLOR):
      if (msMaybeAllocateClassStyle(class, 0)) return MS_FAILURE;
      if(loadColor(&(class->styles[0]->backgroundcolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(COLOR):
      if (msMaybeAllocateClassStyle(class, 0)) return MS_FAILURE;
      if(loadColor(&(class->styles[0]->color), NULL) != MS_SUCCESS) return(-1);
      class->numstyles = 1; /* must *always* set a color or outlinecolor */
      break;
    case(MAXSIZE):
      if (msMaybeAllocateClassStyle(class, 0)) return MS_FAILURE;
      if(getDouble(&(class->styles[0]->maxsize)) == -1) return(-1);
      break;
    case(MINSIZE):      
      if (msMaybeAllocateClassStyle(class, 0)) return MS_FAILURE;
      if(getDouble(&(class->styles[0]->minsize)) == -1) return(-1);
      break;
    case(OUTLINECOLOR):            
      if (msMaybeAllocateClassStyle(class, 0)) return MS_FAILURE;
      if(loadColor(&(class->styles[0]->outlinecolor), NULL) != MS_SUCCESS) return(-1);
      class->numstyles = 1; /* must *always* set a color, symbol or outlinecolor */
      break;
    case(SIZE):
      if (msMaybeAllocateClassStyle(class, 0)) return MS_FAILURE;
      if(getDouble(&(class->styles[0]->size)) == -1) return(-1);
      break;
    case(SYMBOL):
      if (msMaybeAllocateClassStyle(class, 0)) return MS_FAILURE;
      if((state = getSymbol(2, MS_NUMBER,MS_STRING)) == -1) return(-1);
      if(state == MS_NUMBER)
	class->styles[0]->symbol = (int) msyynumber;
      else
      {
        if (class->styles[0]->symbolname != NULL)
          msFree(class->styles[0]->symbolname);
	class->styles[0]->symbolname = msStrdup(msyystring_buffer);
        class->numstyles = 1;
      }
      break;

    /*
    ** for backwards compatability, these are shortcuts for style 1
    */
    case(OVERLAYBACKGROUNDCOLOR):
      if (msMaybeAllocateClassStyle(class, 1)) return MS_FAILURE;
      if(loadColor(&(class->styles[1]->backgroundcolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(OVERLAYCOLOR):
      if (msMaybeAllocateClassStyle(class, 1)) return MS_FAILURE;
      if(loadColor(&(class->styles[1]->color), NULL) != MS_SUCCESS) return(-1);
      class->numstyles = 2; /* must *always* set a color, symbol or outlinecolor */
      break;
    case(OVERLAYMAXSIZE):
      if (msMaybeAllocateClassStyle(class, 1)) return MS_FAILURE;
      if(getDouble(&(class->styles[1]->maxsize)) == -1) return(-1);
      break;
    case(OVERLAYMINSIZE):      
      if (msMaybeAllocateClassStyle(class, 1)) return MS_FAILURE;
      if(getDouble(&(class->styles[1]->minsize)) == -1) return(-1);
      break;
    case(OVERLAYOUTLINECOLOR):      
      if (msMaybeAllocateClassStyle(class, 1)) return MS_FAILURE;
      if(loadColor(&(class->styles[1]->outlinecolor), NULL) != MS_SUCCESS) return(-1);
      class->numstyles = 2; /* must *always* set a color, symbol or outlinecolor */
      break;
    case(OVERLAYSIZE):
      if (msMaybeAllocateClassStyle(class, 1)) return MS_FAILURE;
      if(getDouble(&(class->styles[1]->size)) == -1) return(-1);
      break;
    case(OVERLAYSYMBOL):
      if (msMaybeAllocateClassStyle(class, 1)) return MS_FAILURE;
      if((state = getSymbol(2, MS_NUMBER,MS_STRING)) == -1) return(-1);
      if(state == MS_NUMBER)
	class->styles[1]->symbol = (int) msyynumber;
      else  {
        if (class->styles[1]->symbolname != NULL)
          msFree(class->styles[1]->symbolname);
	class->styles[1]->symbolname = msStrdup(msyystring_buffer);
      }
      class->numstyles = 2;
      break;

    case(VALIDATION):
      if(loadHashTable(&(class->validation)) != MS_SUCCESS) return(-1);
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadClass()", msyystring_buffer, msyylineno);
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  }
}

int msUpdateClassFromString(classObj *class, char *string, int url_string)
{
  int k;
  if(!class || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadClass(class, class->layer) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();

  /* step through styles and labels to resolve symbol names */
  /* class styles */
  for(k=0; k<class->numstyles; k++) {
      if(class->styles[k]->symbolname) {
          if((class->styles[k]->symbol =  msGetSymbolIndex(&(class->layer->map->symbolset), class->styles[k]->symbolname, MS_TRUE)) == -1) {
              msSetError(MS_MISCERR, "Undefined symbol \"%s\" in class, style %d of layer %s.", "msUpdateClassFromString()", class->styles[k]->symbolname, k, class->layer->name);
              return MS_FAILURE;
          }
      }
  }

  /* label styles */
  for(k=0; k<class->label.numstyles; k++) {
      if(class->label.styles[k]->symbolname) {
          if((class->label.styles[k]->symbol =  msGetSymbolIndex(&(class->layer->map->symbolset), class->label.styles[k]->symbolname, MS_TRUE)) == -1) {
              msSetError(MS_MISCERR, "Undefined symbol \"%s\" in class, label style %d of layer %s.", 
                         "msUpdateClassFromString()", class->label.styles[k]->symbolname, k, class->layer->name);
              return MS_FAILURE;
          }
      }
  }

  return MS_SUCCESS;
}

static void writeClass(FILE *stream, int indent, classObj *class)
{
  int i;

  if(class->status == MS_DELETE) return;

  indent++;
  writeBlockBegin(stream, indent, "CLASS");
  writeString(stream, indent, "NAME", NULL, class->name);
  writeString(stream, indent, "GROUP", NULL, class->group);
  writeNumber(stream, indent, "DEBUG", 0, class->debug); 
  writeExpression(stream, indent, "EXPRESSION", &(class->expression));
  writeString(stream, indent, "KEYIMAGE", NULL, class->keyimage);
  writeLabel(stream, indent, &(class->label));
  writeNumber(stream, indent, "MAXSCALEDENOM", -1, class->maxscaledenom);
  writeHashTable(stream, indent, "METADATA", &(class->metadata));
  writeNumber(stream, indent, "MINSCALEDENOM", -1, class->minscaledenom);
  writeNumber(stream, indent, "MINFEATURESIZE", -1, class->minfeaturesize);
  writeKeyword(stream, indent, "STATUS", class->status, 1, MS_OFF, "OFF");
  for(i=0; i<class->numstyles; i++) writeStyle(stream, indent, class->styles[i]);
  writeString(stream, indent, "TEMPLATE", NULL, class->template);
  writeExpression(stream, indent, "TEXT", &(class->text));
  writeString(stream, indent, "TITLE", NULL, class->title);
  writeBlockEnd(stream, indent, "CLASS");
}

/*
** Initialize, load and free a single layer structure
*/
int initLayer(layerObj *layer, mapObj *map)
{
  if (layer==NULL) {
    msSetError(MS_MEMERR, "Layer is null", "initLayer()");
    return(-1);
  }
  layer->debug = (int)msGetGlobalDebugLevel();
  MS_REFCNT_INIT(layer);

  /* Set maxclasses = 0, class[] will be allocated as needed on first call 
   * to msGrowLayerClasses()
   */
  layer->numclasses = 0;
  layer->maxclasses = 0;
  layer->class = NULL;
  
  layer->name = NULL;
  layer->group = NULL;
  layer->status = MS_OFF;
  layer->data = NULL;

  layer->map = map; /* point back to the encompassing structure */

  layer->type = -1;

  layer->annotate = MS_FALSE;

  layer->toleranceunits = MS_PIXELS;
  layer->tolerance = -1; /* perhaps this should have a different value based on type */

  layer->symbolscaledenom = -1.0; /* -1 means nothing is set */
  layer->scalefactor = 1.0;
  layer->maxscaledenom = -1.0;
  layer->minscaledenom = -1.0;
  layer->minfeaturesize = -1; /* no limit */
  layer->maxgeowidth = -1.0;
  layer->mingeowidth = -1.0;

  layer->sizeunits = MS_PIXELS;

  layer->maxfeatures = -1; /* no quota */
  layer->startindex = -1; /*used for pagination*/
  
  layer->template = layer->header = layer->footer = NULL;

  layer->transform = MS_TRUE;

  layer->classitem = NULL;
  layer->classitemindex = -1;

  layer->units = MS_METERS;
  if(msInitProjection(&(layer->projection)) == -1) return(-1);
  layer->project = MS_TRUE;

  initCluster(&layer->cluster);

  MS_INIT_COLOR(layer->offsite, -1,-1,-1, 255);

  layer->labelcache = MS_ON;
  layer->postlabelcache = MS_FALSE;

  layer->labelitem = NULL;
  layer->labelitemindex = -1;

  layer->labelmaxscaledenom = -1;
  layer->labelminscaledenom = -1;

  layer->tileitem = msStrdup("location");
  layer->tileitemindex = -1;
  layer->tileindex = NULL;

  layer->bandsitem = NULL;
  layer->bandsitemindex = -1;

  layer->currentfeature = layer->features = NULL;

  layer->connection = NULL;
  layer->plugin_library = NULL;
  layer->plugin_library_original = NULL;
  layer->connectiontype = MS_SHAPEFILE;
  layer->vtable = NULL;
  layer->classgroup = NULL;

  layer->layerinfo = NULL;
  layer->wfslayerinfo = NULL;

  layer->items = NULL;
  layer->iteminfo = NULL;
  layer->numitems = 0;

  layer->resultcache= NULL;

  initExpression(&(layer->filter));
  layer->filteritem = NULL;
  layer->filteritemindex = -1;

  layer->requires = layer->labelrequires = NULL;

  initHashTable(&(layer->metadata));
  initHashTable(&(layer->bindvals));
  initHashTable(&(layer->validation));
  
  layer->dump = MS_FALSE;

  layer->styleitem = NULL;
  layer->styleitemindex = -1;

  layer->opacity = 100; /* fully opaque */
  
  layer->numprocessing = 0;
  layer->processing = NULL;
  layer->numjoins = 0;
  layer->joins = (joinObj *) malloc(MS_MAXJOINS*sizeof(joinObj));
  MS_CHECK_ALLOC(layer->joins, MS_MAXJOINS*sizeof(joinObj), -1);

  layer->extent.minx = -1.0;
  layer->extent.miny = -1.0;
  layer->extent.maxx = -1.0;
  layer->extent.maxy = -1.0;

  return(0);
}

int freeLayer(layerObj *layer) {
  int i;
  if (!layer) return MS_FAILURE;
  if( MS_REFCNT_DECR_IS_NOT_ZERO(layer) ) { return MS_FAILURE; }

  if (layer->debug >= MS_DEBUGLEVEL_VVV)
     msDebug("freeLayer(): freeing layer at %p.\n",layer);

  if(msLayerIsOpen(layer))
     msLayerClose(layer);

  msFree(layer->name);
  msFree(layer->group);
  msFree(layer->data);
  msFree(layer->classitem);
  msFree(layer->labelitem);
  msFree(layer->header);
  msFree(layer->footer);
  msFree(layer->template);
  msFree(layer->tileindex);
  msFree(layer->tileitem);
  msFree(layer->bandsitem);
  msFree(layer->plugin_library);
  msFree(layer->plugin_library_original);
  msFree(layer->connection);
  msFree(layer->vtable);
  msFree(layer->classgroup);

  msFreeProjection(&(layer->projection));

  freeCluster(&layer->cluster);

  for(i=0;i<layer->maxclasses;i++) {
    if (layer->class[i] != NULL) {
    	layer->class[i]->layer=NULL;
    	if ( freeClass(layer->class[i]) == MS_SUCCESS ) {
		msFree(layer->class[i]);
	}
    }
  }
  msFree(layer->class);

  if(layer->features)
    freeFeatureList(layer->features);

  if(layer->resultcache) {    
    if(layer->resultcache->results) free(layer->resultcache->results);
    msFree(layer->resultcache);
  }

  msFree(layer->styleitem);

  msFree(layer->filteritem);
  freeExpression(&(layer->filter));

  msFree(layer->requires);
  msFree(layer->labelrequires);

  if(&(layer->metadata)) msFreeHashItems(&(layer->metadata));
  if(&(layer->validation)) msFreeHashItems(&(layer->validation));
  if(&(layer->bindvals))  msFreeHashItems(&layer->bindvals);

  if(layer->numprocessing > 0)
      msFreeCharArray(layer->processing, layer->numprocessing);

  for(i=0;i<layer->numjoins;i++) /* each join */
    freeJoin(&(layer->joins[i]));
  msFree(layer->joins);
  layer->numjoins = 0;

  layer->classgroup = NULL;

  return MS_SUCCESS;
}

/*
** Ensure there is at least one free entry in the class array of this 
** layerObj. Grow the allocated class array if necessary and allocate 
** a new class for class[numclasses] if there is not already one, 
** setting its contents to all zero bytes (i.e. does not call initClass()
** on it).
**
** This function is safe to use for the initial allocation of the class[]
** array as well (i.e. when maxclasses==0 and class==NULL)
**
** Returns a reference to the new classObj on success, NULL on error.
*/
classObj *msGrowLayerClasses( layerObj *layer )
{
    /* Do we need to increase the size of class[] by  MS_CLASS_ALLOCSIZE?
     */
    if (layer->numclasses == layer->maxclasses) {
        classObj **newClassPtr;
        int i, newsize;

        newsize = layer->maxclasses + MS_CLASS_ALLOCSIZE;

        /* Alloc/realloc classes */
        newClassPtr = (classObj**)realloc(layer->class,
                                          newsize*sizeof(classObj*));
        MS_CHECK_ALLOC(newClassPtr, newsize*sizeof(classObj*), NULL);

        layer->class = newClassPtr;
        layer->maxclasses = newsize;
        for(i=layer->numclasses; i<layer->maxclasses; i++) {
            layer->class[i] = NULL;
        }
    }

    if (layer->class[layer->numclasses]==NULL) {
        layer->class[layer->numclasses]=(classObj*)calloc(1,sizeof(classObj));
        MS_CHECK_ALLOC(layer->class[layer->numclasses], sizeof(classObj), NULL);
    }

    return layer->class[layer->numclasses];
}

int loadLayer(layerObj *layer, mapObj *map)
{
  int type;

  layer->map = (mapObj *)map;

  for(;;) {
    switch(msyylex()) {
    case(BINDVALS):
      if(loadHashTable(&(layer->bindvals)) != MS_SUCCESS) return(-1);
      break;
    case(CLASS):
      if (msGrowLayerClasses(layer) == NULL)
	return(-1);
      initClass(layer->class[layer->numclasses]);
      if(loadClass(layer->class[layer->numclasses], layer) == -1) return(-1);
      if(layer->class[layer->numclasses]->type == -1) layer->class[layer->numclasses]->type = layer->type;
      layer->numclasses++;
      break;
    case(CLUSTER):
      initCluster(&layer->cluster);
      if(loadCluster(&layer->cluster) == -1) return(-1);
      break;
    case(CLASSGROUP):
      if(getString(&layer->classgroup) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->classgroup, msLookupHashTable(&(layer->validation), "classgroup"), msLookupHashTable(&(map->web.validation), "classgroup"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based CLASSGROUP configuration failed pattern validation." , "loadLayer()");
          msFree(layer->classgroup); layer->classgroup=NULL;
          return(-1);
        }
      }
      break;
    case(CLASSITEM):
      if(getString(&layer->classitem) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->classitem, msLookupHashTable(&(layer->validation), "classitem"), msLookupHashTable(&(map->web.validation), "classitem"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based CLASSITEM configuration failed pattern validation." , "loadLayer()");
          msFree(layer->classitem); layer->classitem=NULL;
          return(-1);
        }
      }
      break;
    case(CONNECTION):
      if(getString(&layer->connection) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->connection, msLookupHashTable(&(layer->validation), "connection"), msLookupHashTable(&(map->web.validation), "connection"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based CONNECTION configuration failed pattern validation." , "loadLayer()");
          msFree(layer->connection); layer->connection=NULL;
          return(-1);
        }
      }
      break;
    case(CONNECTIONTYPE):
      if((layer->connectiontype = getSymbol(9, MS_SDE, MS_OGR, MS_POSTGIS, MS_WMS, MS_ORACLESPATIAL, MS_WFS, MS_GRATICULE, MS_PLUGIN, MS_UNION)) == -1) return(-1);
      break;
    case(DATA):
      if(getString(&layer->data) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->data, msLookupHashTable(&(layer->validation), "data"), msLookupHashTable(&(map->web.validation), "data"), map->datapattern, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based DATA configuration failed pattern validation." , "loadLayer()");
          msFree(layer->data); layer->data=NULL;
          return(-1);
        }
      }
      break;
    case(DEBUG):
      if((layer->debug = getSymbol(3, MS_ON,MS_OFF, MS_NUMBER)) == -1) return(-1);
      if(layer->debug == MS_NUMBER) layer->debug = (int) msyynumber;
      break;
    case(DUMP):
      if((layer->dump = getSymbol(2, MS_TRUE,MS_FALSE)) == -1) return(-1);
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadLayer()");      
      return(-1);
      break;
    case(END):
      if(layer->type == -1) {
	msSetError(MS_MISCERR, "Layer type not set.", "loadLayer()");      
	return(-1);
      }

      return(0);
      break;
    case(EXTENT):
    {
        if(getDouble(&(layer->extent.minx)) == -1) return(-1);
        if(getDouble(&(layer->extent.miny)) == -1) return(-1);
        if(getDouble(&(layer->extent.maxx)) == -1) return(-1);
        if(getDouble(&(layer->extent.maxy)) == -1) return(-1);
        if (!MS_VALID_EXTENT(layer->extent)) {
          msSetError(MS_MISCERR, "Given layer extent is invalid. Check that it is in the form: minx, miny, maxx, maxy", "loadLayer()"); 
          return(-1);
        }
        break;
    }
    case(FEATURE):
      if(layer->type == -1) {
	msSetError(MS_MISCERR, "Layer type must be set before defining inline features.", "loadLayer()");      
	return(-1);
      }

      if(layer->type == MS_LAYER_POLYGON)
	type = MS_SHAPE_POLYGON;
      else if(layer->type == MS_LAYER_LINE)
	type = MS_SHAPE_LINE;
      else
	type = MS_SHAPE_POINT;	
	  
      layer->connectiontype = MS_INLINE;

      if(loadFeature(layer, type) == MS_FAILURE) return(-1);      
      break;
    case(FILTER):
      if(loadExpression(&(layer->filter)) == -1) return(-1); /* loadExpression() cleans up previously allocated expression */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->filter.string, msLookupHashTable(&(layer->validation), "filter"), msLookupHashTable(&(map->web.validation), "filter"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based FILTER configuration failed pattern validation." , "loadLayer()");
          freeExpression(&(layer->filter));
          return(-1);
	}
      }
      break;
    case(FILTERITEM):
      if(getString(&layer->filteritem) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->filteritem, msLookupHashTable(&(layer->validation), "filteritem"), msLookupHashTable(&(map->web.validation), "filteritem"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based FILTERITEM configuration failed pattern validation." , "loadLayer()");
          msFree(layer->filteritem); layer->filteritem=NULL;
          return(-1);
        }
      }
      break;
    case(FOOTER):
      if(getString(&layer->footer) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->footer, msLookupHashTable(&(layer->validation), "footer"), msLookupHashTable(&(map->web.validation), "footer"), map->templatepattern, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based FOOTER configuration failed pattern validation." , "loadLayer()");
          msFree(layer->footer); layer->footer=NULL;
          return(-1);
        }
      }
      break;
    case(GRID):
      layer->connectiontype = MS_GRATICULE;
      layer->layerinfo = (void *) malloc(sizeof(graticuleObj));
      MS_CHECK_ALLOC(layer->layerinfo, sizeof(graticuleObj), -1);

      initGrid((graticuleObj *) layer->layerinfo);
      loadGrid(layer);
      break;
    case(GROUP):
      if(getString(&layer->group) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->group, msLookupHashTable(&(layer->validation), "group"), msLookupHashTable(&(map->web.validation), "group"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based GROUP configuration failed pattern validation." , "loadLayer()");
          msFree(layer->group); layer->group=NULL;
          return(-1);
        }
      }
      break;
    case(HEADER):
      if(getString(&layer->header) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->header, msLookupHashTable(&(layer->validation), "header"), msLookupHashTable(&(map->web.validation), "header"), map->templatepattern, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based HEADER configuration failed pattern validation." , "loadLayer()");
          msFree(layer->header); layer->header=NULL;
          return(-1);
        }
      }
      break;
    case(JOIN):
      if(layer->numjoins == MS_MAXJOINS) { /* no room */
	msSetError(MS_IDENTERR, "Maximum number of joins reached.", "loadLayer()");
	return(-1);
      }

      if(loadJoin(&(layer->joins[layer->numjoins])) == -1) return(-1);
      layer->numjoins++;
      break;
    case(LABELCACHE):
      if((layer->labelcache = getSymbol(2, MS_ON, MS_OFF)) == -1) return(-1);
      break;
    case(LABELITEM):
      if(getString(&layer->labelitem) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->labelitem, msLookupHashTable(&(layer->validation), "labelitem"), msLookupHashTable(&(map->web.validation), "labelitem"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based LABELITEM configuration failed pattern validation." , "loadLayer()");
          msFree(layer->labelitem); layer->labelitem=NULL;
          return(-1);
        }
      }
      break;
    case(LABELMAXSCALE):
    case(LABELMAXSCALEDENOM):
      if(getDouble(&(layer->labelmaxscaledenom)) == -1) return(-1);
      break;
    case(LABELMINSCALE):
    case(LABELMINSCALEDENOM):
      if(getDouble(&(layer->labelminscaledenom)) == -1) return(-1);
      break;    
    case(LABELREQUIRES):
      if(getString(&layer->labelrequires) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->labelrequires, msLookupHashTable(&(layer->validation), "labelrequires"), msLookupHashTable(&(map->web.validation), "labelrequires"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based LABELREQUIRES configuration failed pattern validation." , "loadLayer()");
          msFree(layer->labelrequires); layer->labelrequires=NULL;
          return(-1);
        }
      }
      break;
    case(LAYER):
      break; /* for string loads */
    case(MAXFEATURES):
      if(getInteger(&(layer->maxfeatures)) == -1) return(-1);
      break;
    case(MAXSCALE):
    case(MAXSCALEDENOM):
      if(getDouble(&(layer->maxscaledenom)) == -1) return(-1);
      break;
    case(MAXGEOWIDTH):
      if(getDouble(&(layer->maxgeowidth)) == -1) return(-1);
      break;
    case(METADATA):
      if(loadHashTable(&(layer->metadata)) != MS_SUCCESS) return(-1);
      break;
    case(MINSCALE):
    case(MINSCALEDENOM):
      if(getDouble(&(layer->minscaledenom)) == -1) return(-1);
      break;
    case(MINGEOWIDTH):
      if(getDouble(&(layer->mingeowidth)) == -1) return(-1);
      break;
    case(MINFEATURESIZE):
      if(getInteger(&(layer->minfeaturesize)) == -1) return(-1);
      break;
    case(NAME):
      if(getString(&layer->name) == MS_FAILURE) return(-1);
      break;
    case(OFFSITE):
      if(loadColor(&(layer->offsite), NULL) != MS_SUCCESS) return(-1);
      break;
    case(OPACITY):
    case(TRANSPARENCY): /* keyword supported for mapfile backwards compatability */
      if (getIntegerOrSymbol(&(layer->opacity), 1, MS_GD_ALPHA) == -1)
        return(-1);
      break;
    case(MS_PLUGIN): 
    {
        int rv;
        if(getString(&layer->plugin_library_original) == MS_FAILURE) return(-1);
        rv = msBuildPluginLibraryPath(&layer->plugin_library, 
                                      layer->plugin_library_original, 
                                      map);
        if (rv == MS_FAILURE) return(-1);
    }
    break;
    case(PROCESSING):
    {
      /* NOTE: processing array maintained as size+1 with NULL terminator.
               This ensure that CSL (GDAL string list) functions can be
               used on the list for easy processing. */
      char *value=NULL;
      if(getString(&value) == MS_FAILURE) return(-1);
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(value, msLookupHashTable(&(layer->validation), "processing"), msLookupHashTable(&(map->web.validation), "processing"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based PROCESSING configuration failed pattern validation." , "loadLayer()");
          free(value); value=NULL;
          return(-1);
        }
      }
      msLayerAddProcessing( layer, value );
      free(value); value=NULL;
    }
    break;
    case(POSTLABELCACHE):
      if((layer->postlabelcache = getSymbol(2, MS_TRUE, MS_FALSE)) == -1) return(-1);
      if(layer->postlabelcache)
	layer->labelcache = MS_OFF;
      break;
    case(PROJECTION):
      if(loadProjection(&(layer->projection)) == -1) return(-1);
      layer->project = MS_TRUE;
      break;
    case(REQUIRES):
      if(getString(&layer->requires) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->requires, msLookupHashTable(&(layer->validation), "requires"), msLookupHashTable(&(map->web.validation), "requires"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based REQUIRES configuration failed pattern validation." , "loadLayer()");
          msFree(layer->requires); layer->requires=NULL;
          return(-1);
        }
      }
      break;
    case(SIZEUNITS):
      if((layer->sizeunits = getSymbol(8, MS_INCHES,MS_FEET,MS_MILES,MS_METERS,MS_KILOMETERS,MS_NAUTICALMILES,MS_DD,MS_PIXELS)) == -1) return(-1);
      break;
    case(STATUS):
      if((layer->status = getSymbol(3, MS_ON,MS_OFF,MS_DEFAULT)) == -1) return(-1);
      break;
    case(STYLEITEM):
      if(getString(&layer->styleitem) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->styleitem, msLookupHashTable(&(layer->validation), "styleitem"), msLookupHashTable(&(map->web.validation), "styleitem"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based STYLEITEM configuration failed pattern validation." , "loadLayer()");
          msFree(layer->styleitem); layer->styleitem=NULL;
          return(-1);
        }
      }
      break;
    case(SYMBOLSCALE):
    case(SYMBOLSCALEDENOM):
      if(getDouble(&(layer->symbolscaledenom)) == -1) return(-1);
      break;
    case(TEMPLATE):
      if(getString(&layer->template) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->template, msLookupHashTable(&(layer->validation), "template"), msLookupHashTable(&(map->web.validation), "template"), map->templatepattern, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based TEMPLATE configuration failed pattern validation." , "loadLayer()");
          msFree(layer->template); layer->template=NULL;
          return(-1);
        }
      }
      break;
    case(TILEINDEX):
      if(getString(&layer->tileindex) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->tileindex, msLookupHashTable(&(layer->validation), "tileindex"), msLookupHashTable(&(map->web.validation), "tileindex"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based TILEINDEX configuration failed pattern validation." , "loadLayer()");
          msFree(layer->tileindex); layer->tileindex=NULL;
          return(-1);
        }
      }
      break;
    case(TILEITEM):
      if(getString(&layer->tileitem) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(layer->tileitem, msLookupHashTable(&(layer->validation), "tileitem"), msLookupHashTable(&(map->web.validation), "tileitem"), NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based TILEITEM configuration failed pattern validation." , "loadLayer()");
          msFree(layer->tileitem); layer->tileitem=NULL;
          return(-1);
        }
      }
      break;
    case(TOLERANCE):
      if(getDouble(&(layer->tolerance)) == -1) return(-1);
      break;
    case(TOLERANCEUNITS):
      if((layer->toleranceunits = getSymbol(8, MS_INCHES,MS_FEET,MS_MILES,MS_METERS,MS_KILOMETERS,MS_NAUTICALMILES,MS_DD,MS_PIXELS)) == -1) return(-1);
      break;
    case(TRANSFORM):
      if((layer->transform = getSymbol(11, MS_TRUE,MS_FALSE, MS_UL,MS_UC,MS_UR,MS_CL,MS_CC,MS_CR,MS_LL,MS_LC,MS_LR)) == -1) return(-1);
      break;
    case(TYPE):
      if((layer->type = getSymbol(9, MS_LAYER_POINT,MS_LAYER_LINE,MS_LAYER_RASTER,MS_LAYER_POLYGON,MS_LAYER_ANNOTATION,MS_LAYER_QUERY,MS_LAYER_CIRCLE,MS_LAYER_CHART,TILEINDEX)) == -1) return(-1);
      if(layer->type == TILEINDEX) layer->type = MS_LAYER_TILEINDEX; /* TILEINDEX is also a parameter */
      break;    
    case(UNITS):
      if((layer->units = getSymbol(9, MS_INCHES,MS_FEET,MS_MILES,MS_METERS,MS_KILOMETERS,MS_NAUTICALMILES,MS_DD,MS_PIXELS,MS_PERCENTAGES)) == -1) return(-1);
      break;
    case(VALIDATION):
      if(loadHashTable(&(layer->validation)) != MS_SUCCESS) return(-1);
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadLayer()", msyystring_buffer, msyylineno);      
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  } /* next token */
}

int msUpdateLayerFromString(layerObj *layer, char *string, int url_string)
{
  int j, k;

  if(!layer || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadLayer(layer, layer->map) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();

  /* step through classes to resolve symbol names */
  for(j=0; j<layer->numclasses; j++) {

      /* class styles */
      for(k=0; k<layer->class[j]->numstyles; k++) {
          if(layer->class[j]->styles[k]->symbolname) {
              if((layer->class[j]->styles[k]->symbol =  msGetSymbolIndex(&(layer->map->symbolset), layer->class[j]->styles[k]->symbolname, MS_TRUE)) == -1) {
                  msSetError(MS_MISCERR, "Undefined symbol \"%s\" in class %d, style %d of layer %s.", "msUpdateLayerFromString()", layer->class[j]->styles[k]->symbolname, j, k, layer->name);
                  return MS_FAILURE;
              }
          }
      }

      /* label styles */
      for(k=0; k<layer->class[j]->label.numstyles; k++) {
          if(layer->class[j]->label.styles[k]->symbolname) {
              if((layer->class[j]->label.styles[k]->symbol =  msGetSymbolIndex(&(layer->map->symbolset), layer->class[j]->label.styles[k]->symbolname, MS_TRUE)) == -1) {
                  msSetError(MS_MISCERR, "Undefined symbol \"%s\" in class %d, label style %d of layer %s.", 
                             "msUpdateLayerFromString()", layer->class[j]->label.styles[k]->symbolname, j, k, layer->name);
                  return MS_FAILURE;
              }
          }
      }          
  }

  return MS_SUCCESS;
}

static void writeLayer(FILE *stream, int indent, layerObj *layer)
{
  int i;
  featureListNodeObjPtr current=NULL;

  if(layer->status == MS_DELETE)
    return;

  indent++;
  writeBlockBegin(stream, indent, "LAYER");
  // bindvals
  /* class - see below */
  writeString(stream, indent, "CLASSGROUP", NULL, layer->classgroup);
  writeString(stream, indent, "CLASSITEM", NULL, layer->classitem);
  writeString(stream, indent, "CONNECTION", NULL, layer->connection);
  writeKeyword(stream, indent, "CONNECTIONTYPE", layer->connectiontype, 9, MS_SDE, "SDE", MS_OGR, "OGR", MS_POSTGIS, "POSTGIS", MS_WMS, "WMS", MS_ORACLESPATIAL, "ORACLESPATIAL", MS_WFS, "WFS", MS_GRATICULE, "GRATICULE", MS_PLUGIN, "PLUGIN", MS_UNION, "UNION");
  writeString(stream, indent, "DATA", NULL, layer->data);
  writeNumber(stream, indent, "DEBUG", 0, layer->debug); /* is this right? see loadLayer() */ 
  writeKeyword(stream, indent, "DUMP", layer->dump, 1, MS_TRUE, "TRUE");
  writeExtent(stream, indent, "EXTENT", layer->extent);
  writeExpression(stream, indent, "FILTER", &(layer->filter));
  writeString(stream, indent, "FILTERITEM", NULL, layer->filteritem);
  writeString(stream, indent, "FOOTER", NULL, layer->footer);
  writeString(stream, indent, "GROUP", NULL, layer->group);
  writeString(stream, indent, "HEADER", NULL, layer->header);
  /* join - see below */
  writeKeyword(stream, indent, "LABELCACHE", layer->labelcache, 1, MS_OFF, "OFF");
  writeString(stream, indent, "LABELITEM", NULL, layer->labelitem);
  writeNumber(stream, indent, "LABELMAXSCALEDENOM", -1, layer->labelmaxscaledenom);
  writeNumber(stream, indent, "LABELMINSCALEDENOM", -1, layer->labelminscaledenom);
  writeString(stream, indent, "LABELREQUIRES", NULL, layer->labelrequires);
  writeNumber(stream, indent, "MAXFEATURES", -1, layer->maxfeatures);
  writeNumber(stream, indent, "MAXGEOWIDTH", -1, layer->maxgeowidth);
  writeNumber(stream, indent, "MAXSCALEDENOM", -1, layer->maxscaledenom);
  writeHashTable(stream, indent, "METADATA", &(layer->metadata));
  writeNumber(stream, indent, "MINGEOWIDTH", -1, layer->mingeowidth);
  writeNumber(stream, indent, "MINSCALEDENOM", -1, layer->minscaledenom);
  writeNumber(stream, indent, "MINFEATURESIZE", -1, layer->minfeaturesize);
  writeString(stream, indent, "NAME", NULL, layer->name);
  writeColor(stream, indent, "OFFSITE", &(layer->offsite));
  writeString(stream, indent, "PLUGIN", NULL, layer->plugin_library_original);
  writeKeyword(stream, indent, "POSTLABELCACHE", layer->postlabelcache, 1, MS_TRUE, "TRUE");
  for(i=0; i<layer->numprocessing; i++)
    writeString(stream, indent, "PROCESSING", NULL, layer->processing[i]);
  writeProjection(stream, indent, &(layer->projection));
  writeString(stream, indent, "REQUIRES", NULL, layer->requires);
  writeKeyword(stream, indent, "SIZEUNITS", layer->sizeunits, 7, MS_INCHES, "INCHES", MS_FEET ,"FEET", MS_MILES, "MILES", MS_METERS, "METERS", MS_KILOMETERS, "KILOMETERS", MS_NAUTICALMILES, "NAUTICALMILES", MS_DD, "DD");
  writeKeyword(stream, indent, "STATUS", layer->status, 3, MS_ON, "ON", MS_OFF, "OFF", MS_DEFAULT, "DEFAULT");
  writeString(stream, indent, "STYLEITEM", NULL, layer->styleitem);
  writeNumber(stream, indent, "SYMBOLSCALEDENOM", -1, layer->symbolscaledenom);
  writeString(stream, indent, "TEMPLATE", NULL, layer->template);
  writeString(stream, indent, "TILEINDEX", NULL, layer->tileindex);
  writeString(stream, indent, "TILEITEM", NULL, layer->tileitem);
  writeNumber(stream, indent, "TOLERANCE", -1, layer->tolerance);
  writeKeyword(stream, indent, "TOLERANCEUNITS", layer->toleranceunits, 7, MS_INCHES, "INCHES", MS_FEET ,"FEET", MS_MILES, "MILES", MS_METERS, "METERS", MS_KILOMETERS, "KILOMETERS", MS_NAUTICALMILES, "NAUTICALMILES", MS_DD, "DD");
  writeKeyword(stream, indent, "TRANSFORM", layer->transform, 10, MS_FALSE, "FALSE", MS_UL, "UL", MS_UC, "UC", MS_UR, "UR", MS_CL, "CL", MS_CC, "CC", MS_CR, "CR", MS_LL, "LL", MS_LC, "LC", MS_LR, "LR");
  writeNumberOrKeyword(stream, indent, "OPACITY", 100, layer->opacity, (int)layer->opacity, 1, MS_GD_ALPHA, "ALPHA"); 
  writeKeyword(stream, indent, "TYPE", layer->type, 9, MS_LAYER_POINT, "POINT", MS_LAYER_LINE, "LINE", MS_LAYER_POLYGON, "POLYGON", MS_LAYER_RASTER, "RASTER", MS_LAYER_ANNOTATION, "ANNOTATION", MS_LAYER_QUERY, "QUERY", MS_LAYER_CIRCLE, "CIRCLE", MS_LAYER_TILEINDEX, "TILEINDEX", MS_LAYER_CHART, "CHART");
  writeKeyword(stream, indent, "UNITS", layer->units, 9, MS_INCHES, "INCHES", MS_FEET ,"FEET", MS_MILES, "MILES", MS_METERS, "METERS", MS_KILOMETERS, "KILOMETERS", MS_NAUTICALMILES, "NAUTICALMILES", MS_DD, "DD", MS_PIXELS, "PIXELS", MS_PERCENTAGES, "PERCENTATGES");
  writeHashTable(stream, indent, "VALIDATION", &(layer->validation));

  /* write potentially multiply occuring objects last */
  for(i=0; i<layer->numjoins; i++)  writeJoin(stream, indent, &(layer->joins[i]));
  for(i=0; i<layer->numclasses; i++) writeClass(stream, indent, layer->class[i]);

  if( layer->layerinfo &&  layer->connectiontype == MS_GRATICULE)
    writeGrid(stream, indent, (graticuleObj *) layer->layerinfo);
  else {
    current = layer->features;
    while(current != NULL) {
      writeFeature(stream, indent, &(current->shape));
      current = current->next;
    }
  }

  writeBlockEnd(stream, indent, "LAYER");
  writeLineFeed(stream);
}

/*
** Initialize, load and free a referenceMapObj structure
*/
void initReferenceMap(referenceMapObj *ref)
{
  ref->height = ref->width = 0;
  ref->extent.minx = ref->extent.miny = ref->extent.maxx = ref->extent.maxy = -1.0;
  ref->image = NULL;
  MS_INIT_COLOR(ref->color, 255, 0, 0, 255);
  MS_INIT_COLOR(ref->outlinecolor, 0, 0, 0, 255);  
  ref->status = MS_OFF;
  ref->marker = 0;
  ref->markername = NULL;
  ref->markersize = 0;
  ref->minboxsize = 3;
  ref->maxboxsize = 0;
  ref->map = NULL;
}

void freeReferenceMap(referenceMapObj *ref)
{
  msFree(ref->image);
  msFree(ref->markername);
}

int loadReferenceMap(referenceMapObj *ref, mapObj *map)
{
  int state;

  ref->map = (mapObj *)map;

  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadReferenceMap()");
      return(-1);
    case(END):
      if(!ref->image) {
	msSetError(MS_MISCERR, "No image defined for the reference map.", "loadReferenceMap()");
	return(-1);
      }
      if(ref->width == 0 || ref->height == 0) {
	msSetError(MS_MISCERR, "No image size defined for the reference map.", "loadReferenceMap()");
	return(-1);
      }
      return(0);
      break;
    case(COLOR):
      if(loadColor(&(ref->color), NULL) != MS_SUCCESS) return(-1);
      break;
    case(EXTENT):
      if(getDouble(&(ref->extent.minx)) == -1) return(-1);
      if(getDouble(&(ref->extent.miny)) == -1) return(-1);
      if(getDouble(&(ref->extent.maxx)) == -1) return(-1);
      if(getDouble(&(ref->extent.maxy)) == -1) return(-1);
      if (!MS_VALID_EXTENT(ref->extent)) {
      	msSetError(MS_MISCERR, "Given reference extent is invalid. Check that it " \
        "is in the form: minx, miny, maxx, maxy", "loadReferenceMap()"); 
        return(-1);
      	}
      break;  
    case(IMAGE):
      if(getString(&ref->image) == MS_FAILURE) return(-1);
      break;
    case(OUTLINECOLOR):
      if(loadColor(&(ref->outlinecolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(SIZE):
      if(getInteger(&(ref->width)) == -1) return(-1);
      if(getInteger(&(ref->height)) == -1) return(-1);
      break;          
    case(STATUS):
      if((ref->status = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;
    case(MARKER):
      if((state = getSymbol(2, MS_NUMBER,MS_STRING)) == -1) return(-1);

      if(state == MS_NUMBER)
	ref->marker = (int) msyynumber;
      else
      {
        if (ref->markername != NULL)
          msFree(ref->markername);
	ref->markername = msStrdup(msyystring_buffer);
      }
      break;
    case(MARKERSIZE):
      if(getInteger(&(ref->markersize)) == -1) return(-1);
      break;
    case(MINBOXSIZE):
      if(getInteger(&(ref->minboxsize)) == -1) return(-1);
      break;
    case(MAXBOXSIZE):
      if(getInteger(&(ref->maxboxsize)) == -1) return(-1);
      break;
    case(REFERENCE):
      break; /* for string loads */
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadReferenceMap()", msyystring_buffer, msyylineno);
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  } /* next token */
}

int msUpdateReferenceMapFromString(referenceMapObj *ref, char *string, int url_string)
{
  if(!ref || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadReferenceMap(ref, ref->map) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

static void writeReferenceMap(FILE *stream, int indent, referenceMapObj *ref)
{
  if(!ref->image) return;

  indent++;
  writeBlockBegin(stream, indent, "REFERENCE");
  writeColor(stream, indent, "COLOR", &(ref->color));
  writeExtent(stream, indent, "EXTENT", ref->extent);
  writeString(stream, indent, "IMAGE", NULL, ref->image);
  writeColor(stream, indent, "OUTLINECOLOR", &(ref->outlinecolor));
  writeDimension(stream, indent, "SIZE", ref->width, ref->height, NULL, NULL);
  writeKeyword(stream, indent, "STATUS", ref->status, 2, MS_ON, "ON", MS_OFF, "OFF");
  writeNumberOrString(stream, indent, "MARKER", 0, ref->marker, ref->markername);
  writeNumber(stream, indent, "MARKERSIZE", -1, ref->markersize);
  writeNumber(stream, indent, "MAXBOXSIZE", -1, ref->maxboxsize);
  writeNumber(stream, indent, "MINBOXSIZE", -1, ref->minboxsize);
  writeBlockEnd(stream, indent, "REFERENCE");
  writeLineFeed(stream);
}

#define MAX_FORMATOPTIONS 100

static int loadOutputFormat(mapObj *map)
{
  char *name = NULL;
  char *mimetype = NULL;
  char *driver = NULL;
  char *extension = NULL;
  int imagemode = MS_NOOVERRIDE;
  int transparent = MS_NOOVERRIDE;
  char *formatoptions[MAX_FORMATOPTIONS];
  int  numformatoptions = 0;
  char *value = NULL;

  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadOutputFormat()");      
      return(-1);

    case(END):
    {
        outputFormatObj *format;

        if( driver == NULL )
        {
            msSetError(MS_MISCERR, 
                       "OUTPUTFORMAT clause lacks DRIVER keyword near (%s):(%d)",
                       "loadOutputFormat()",
                       msyystring_buffer, msyylineno );
            return -1;
        }
        
        format = msCreateDefaultOutputFormat( map, driver, name );
        if( format == NULL )
        {
            msSetError(MS_MISCERR, 
                       "OUTPUTFORMAT clause references driver %s, but this driver isn't configured.", 
                       "loadOutputFormat()", driver );
            return -1;
        }
        msFree( driver );

        if( transparent != MS_NOOVERRIDE )
            format->transparent = transparent;
        if( extension != NULL )
        {
            msFree( format->extension );
            format->extension = extension;
        }
        if( mimetype != NULL )
        {
            msFree( format->mimetype );
            format->mimetype = mimetype;
        }
        if( imagemode != MS_NOOVERRIDE )
        {
            format->imagemode = imagemode;

            if( transparent == MS_NOOVERRIDE )
            {
                if( imagemode == MS_IMAGEMODE_RGB  )
                    format->transparent = MS_FALSE;
                else if( imagemode == MS_IMAGEMODE_RGBA  )
                    format->transparent = MS_TRUE;
            }
            if( format->imagemode == MS_IMAGEMODE_INT16 
                || format->imagemode == MS_IMAGEMODE_FLOAT32 
                || format->imagemode == MS_IMAGEMODE_BYTE )
                format->renderer = MS_RENDER_WITH_RAWDATA;
            if( format->imagemode == MS_IMAGEMODE_PC256 )
                format->renderer = MS_RENDER_WITH_GD;
        }

        format->numformatoptions = numformatoptions;
        if( numformatoptions > 0 )
        {
            format->formatoptions = (char **) 
                msSmallMalloc(sizeof(char *)*numformatoptions );
            memcpy( format->formatoptions, formatoptions, 
                    sizeof(char *)*numformatoptions );
        }

        format->inmapfile = MS_TRUE;

        if( !msOutputFormatValidate( format, MS_TRUE ) )
            return -1;
        return(0);
    }
    case(NAME):
      msFree( name );
      if((name = getToken()) == NULL) return(-1);
      break;
    case(MIMETYPE):
      if(getString(&mimetype) == MS_FAILURE) return(-1);
      break;
    case(DRIVER):
      {
        int s;
        if((s = getSymbol(2, MS_STRING, TEMPLATE)) == -1) return -1; /* allow the template to be quoted or not in the mapfile */
        if(s == MS_STRING)
          driver = msStrdup(msyystring_buffer);
        else
          driver = msStrdup("TEMPLATE");
      }
      break;
    case(EXTENSION):
      if(getString(&extension) == MS_FAILURE) return(-1);
      if( extension[0] == '.' )
      {
          char *temp = msStrdup(extension+1);
          free( extension );
          extension = temp;
      }
      break;
    case(FORMATOPTION):
      if(getString(&value) == MS_FAILURE) return(-1);
      if( numformatoptions < MAX_FORMATOPTIONS )
          formatoptions[numformatoptions++] = msStrdup(value);
      free(value); value=NULL;
      break;
    case(IMAGEMODE):
      value = getToken();
      if( strcasecmp(value,"PC256") == 0 )
          imagemode = MS_IMAGEMODE_PC256;
      else if( strcasecmp(value,"RGB") == 0 )
          imagemode = MS_IMAGEMODE_RGB;
      else if( strcasecmp(value,"RGBA") == 0)
          imagemode = MS_IMAGEMODE_RGBA;
      else if( strcasecmp(value,"INT16") == 0)
          imagemode = MS_IMAGEMODE_INT16;
      else if( strcasecmp(value,"FLOAT32") == 0)
          imagemode = MS_IMAGEMODE_FLOAT32;
      else if( strcasecmp(value,"BYTE") == 0)
          imagemode = MS_IMAGEMODE_BYTE;
      else if( strcasecmp(value,"FEATURE") == 0)
          imagemode = MS_IMAGEMODE_FEATURE;
      else
      {
          msSetError(MS_IDENTERR, 
                     "Parsing error near (%s):(line %d), expected PC256, RGB, RGBA, FEATURE, BYTE, INT16, or FLOAT32 for IMAGEMODE.", "loadOutputFormat()", 
                     msyystring_buffer, msyylineno);      
          return -1;
      }
      free(value); value=NULL;
      break;
    case(TRANSPARENT):
      if((transparent = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;
    default:
      msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadOutputFormat()", 
                 msyystring_buffer, msyylineno);      
      return(-1);
    }
  } /* next token */
}

/*
** utility function to write an output format structure to file
*/
static void writeOutputformatobject(FILE *stream, int indent, outputFormatObj *outputformat)
{
  int i = 0;
  if(!outputformat) return;

  indent++;
  writeBlockBegin(stream, indent, "OUTPUTFORMAT");
  writeString(stream, indent, "NAME", NULL, outputformat->name);
  writeString(stream, indent, "MIMETYPE", NULL, outputformat->mimetype);
  writeString(stream, indent, "DRIVER", NULL, outputformat->driver);
  writeString(stream, indent, "EXTENSION", NULL, outputformat->extension);
  writeKeyword(stream, indent, "IMAGEMODE", outputformat->imagemode, 7, MS_IMAGEMODE_PC256, "PC256", MS_IMAGEMODE_RGB, "RGB", MS_IMAGEMODE_RGBA, "RGBA", MS_IMAGEMODE_INT16, "INT16", MS_IMAGEMODE_FLOAT32, "FLOAT32", MS_IMAGEMODE_BYTE, "BYTE", MS_IMAGEMODE_FEATURE, "FEATURE");
  writeKeyword(stream, indent, "TRANSPARENT", outputformat->transparent, 2, MS_TRUE, "TRUE", MS_FALSE, "FALSE");
  for (i=0; i<outputformat->numformatoptions; i++) 
    writeString(stream, indent, "FORMATOPTION", NULL, outputformat->formatoptions[i]);
  writeBlockEnd(stream, indent, "OUTPUTFORMAT");
  writeLineFeed(stream);
}


/*
** Write the output formats to file
*/
static void writeOutputformat(FILE *stream, int indent, mapObj *map)
{
  int i=0;
  if(!map->outputformat) return;

  writeOutputformatobject(stream, indent, map->outputformat);
  for(i=0; i<map->numoutputformats; i++) {
    if(map->outputformatlist[i]->inmapfile == MS_TRUE && strcmp(map->outputformatlist[i]->driver, map->outputformat->driver) != 0)
      writeOutputformatobject(stream, indent, map->outputformatlist[i]);
  }
}
                              
/*
** Initialize, load and free a legendObj structure
*/
void initLegend(legendObj *legend)
{
  legend->height = legend->width = 0; 
  MS_INIT_COLOR(legend->imagecolor, 255,255,255,255); /* white */
  MS_INIT_COLOR(legend->outlinecolor, -1,-1,-1,255);
  initLabel(&legend->label);
  legend->label.position = MS_XY; /* override */
  legend->keysizex = 20;
  legend->keysizey = 10;
  legend->keyspacingx = 5;
  legend->keyspacingy = 5;  
  legend->status = MS_OFF;
  legend->transparent = MS_NOOVERRIDE;
  legend->interlace = MS_NOOVERRIDE;
  legend->position = MS_LL;
  legend->postlabelcache = MS_FALSE; /* draw with labels */
  legend->template = NULL;
  legend->map = NULL;
}

void freeLegend(legendObj *legend)
{
  if (legend->template)
     free(legend->template);
  freeLabel(&(legend->label));
}

int loadLegend(legendObj *legend, mapObj *map)
{
  legend->map = (mapObj *)map;

  for(;;) {
    switch(msyylex()) {
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadLegend()");      
      return(-1);
    case(END):
      legend->label.position = MS_XY; /* overrides go here */
      return(0);
      break;
    case(IMAGECOLOR):
      if(loadColor(&(legend->imagecolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(INTERLACE):
      if((legend->interlace = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;
    case(KEYSIZE):
      if(getInteger(&(legend->keysizex)) == -1) return(-1);
      if(getInteger(&(legend->keysizey)) == -1) return(-1);
      break;      
    case(KEYSPACING):
      if(getInteger(&(legend->keyspacingx)) == -1) return(-1);
      if(getInteger(&(legend->keyspacingy)) == -1) return(-1);
      break;  
    case(LABEL):
      if(loadLabel(&(legend->label)) == -1) return(-1);
      legend->label.angle = 0; /* force */
      break;
    case(LEGEND):
      break; /* for string loads */
    case(OUTLINECOLOR):     
      if(loadColor(&(legend->outlinecolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(POSITION):
      if((legend->position = getSymbol(6, MS_UL,MS_UR,MS_LL,MS_LR,MS_UC,MS_LC)) == -1) return(-1);
      break;
    case(POSTLABELCACHE):
      if((legend->postlabelcache = getSymbol(2, MS_TRUE,MS_FALSE)) == -1) return(-1);      
      break;
    case(STATUS):
      if((legend->status = getSymbol(3, MS_ON,MS_OFF,MS_EMBED)) == -1) return(-1);      
      break;
    case(TRANSPARENT):
      if((legend->transparent = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;
    case(TEMPLATE):
      if(getString(&legend->template) == MS_FAILURE) return(-1);
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadLegend()", msyystring_buffer, msyylineno);
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  } /* next token */
}

int msUpdateLegendFromString(legendObj *legend, char *string, int url_string)
{
  if(!legend || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadLegend(legend, legend->map) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

static void writeLegend(FILE *stream, int indent, legendObj *legend)
{
  indent++;
  writeBlockBegin(stream, indent, "LEGEND");
  writeColor(stream, indent, "IMAGECOLOR", &(legend->imagecolor));
  writeKeyword(stream, indent, "INTERLACE", legend->interlace, 2, MS_TRUE, "TRUE", MS_FALSE, "FALSE");
  writeDimension(stream, indent, "KEYSIZE", legend->keysizex, legend->keysizey, NULL, NULL);
  writeDimension(stream, indent, "KEYSPACING", legend->keyspacingx, legend->keyspacingy, NULL, NULL);
  writeLabel(stream, indent, &(legend->label));
  writeColor(stream, indent, "OUTLINECOLOR", &(legend->outlinecolor));
  if(legend->status == MS_EMBED) writeKeyword(stream, indent, "POSITION", legend->position, 6, MS_LL, "LL", MS_UL, "UL", MS_UR, "UR", MS_LR, "LR", MS_UC, "UC", MS_LC, "LC");
  writeKeyword(stream, indent, "POSTLABELCACHE", legend->postlabelcache, 1, MS_TRUE, "TRUE");
  writeKeyword(stream, indent, "STATUS", legend->status, 3, MS_ON, "ON", MS_OFF, "OFF", MS_EMBED, "EMBED");
  writeKeyword(stream, indent, "TRANSPARENT", legend->transparent, 2, MS_TRUE, "TRUE", MS_FALSE, "FALSE");
  writeString(stream, indent, "TEMPLATE", NULL, legend->template);
  writeBlockEnd(stream, indent, "LEGEND");
  writeLineFeed(stream);
}

/*
** Initialize, load and free a scalebarObj structure
*/
void initScalebar(scalebarObj *scalebar)
{
  MS_INIT_COLOR(scalebar->imagecolor, 255,255,255,255);
  scalebar->width = 200; 
  scalebar->height = 3;
  scalebar->style = 0; /* only 2 styles at this point */
  scalebar->intervals = 4;
  initLabel(&scalebar->label);
  scalebar->label.position = MS_XY; /* override */
  MS_INIT_COLOR(scalebar->backgroundcolor, -1,-1,-1,255);  /* if not set, scalebar creation needs to set this to match the background color */
  MS_INIT_COLOR(scalebar->color, 0,0,0,255); /* default to black */
  MS_INIT_COLOR(scalebar->outlinecolor, -1,-1,-1,255);
  scalebar->units = MS_MILES;
  scalebar->status = MS_OFF;
  scalebar->position = MS_LL;
  scalebar->transparent = MS_NOOVERRIDE; /* no transparency */
  scalebar->interlace = MS_NOOVERRIDE;
  scalebar->postlabelcache = MS_FALSE; /* draw with labels */
  scalebar->align = MS_ALIGN_CENTER;
}

void freeScalebar(scalebarObj *scalebar) {
  freeLabel(&(scalebar->label));
}

int loadScalebar(scalebarObj *scalebar)
{
  for(;;) {
    switch(msyylex()) {
    case(ALIGN):
      if((scalebar->align = getSymbol(3, MS_ALIGN_LEFT,MS_ALIGN_CENTER,MS_ALIGN_RIGHT)) == -1) return(-1);
      break;
    case(BACKGROUNDCOLOR):            
      if(loadColor(&(scalebar->backgroundcolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(COLOR):
      if(loadColor(&(scalebar->color), NULL) != MS_SUCCESS) return(-1);   
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadScalebar()");      
      return(-1);
    case(END):
      return(0);
      break;
    case(IMAGECOLOR):      
      if(loadColor(&(scalebar->imagecolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(INTERLACE):
      if((scalebar->interlace = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;
    case(INTERVALS):      
      if(getInteger(&(scalebar->intervals)) == -1) return(-1);
      break;
    case(LABEL):
      if(loadLabel(&(scalebar->label)) == -1) return(-1);
      scalebar->label.angle = 0;
      break;
    case(OUTLINECOLOR):      
      if(loadColor(&(scalebar->outlinecolor), NULL) != MS_SUCCESS) return(-1);
      break;
    case(POSITION):
      if((scalebar->position = getSymbol(6, MS_UL,MS_UR,MS_LL,MS_LR,MS_UC,MS_LC)) == -1) 
	return(-1);
      break;
    case(POSTLABELCACHE):
      if((scalebar->postlabelcache = getSymbol(2, MS_TRUE,MS_FALSE)) == -1) return(-1);
      break;
    case(SCALEBAR):
      break; /* for string loads */
    case(SIZE):
      if(getInteger(&(scalebar->width)) == -1) return(-1);
      if(getInteger(&(scalebar->height)) == -1) return(-1);
      break;
    case(STATUS):
      if((scalebar->status = getSymbol(3, MS_ON,MS_OFF,MS_EMBED)) == -1) return(-1);
      break;
    case(STYLE):      
      if(getInteger(&(scalebar->style)) == -1) return(-1);
      break;  
    case(TRANSPARENT):
      if((scalebar->transparent = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;
    case(UNITS):
      if((scalebar->units = getSymbol(6, MS_INCHES,MS_FEET,MS_MILES,MS_METERS,MS_KILOMETERS,MS_NAUTICALMILES)) == -1) return(-1);
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadScalebar()", msyystring_buffer, msyylineno);
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  } /* next token */
}

int msUpdateScalebarFromString(scalebarObj *scalebar, char *string, int url_string)
{
  if(!scalebar || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadScalebar(scalebar) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

static void writeScalebar(FILE *stream, int indent, scalebarObj *scalebar)
{
  indent++;
  writeBlockBegin(stream, indent, "SCALEBAR");
  writeKeyword(stream, indent, "ALIGN", scalebar->align, 2, MS_ALIGN_LEFT, "LEFT", MS_ALIGN_RIGHT, "RIGHT");
  writeColor(stream, indent, "BACKGROUNDCOLOR", &(scalebar->backgroundcolor));
  writeColor(stream, indent, "COLOR", &(scalebar->color));
  writeColor(stream, indent, "IMAGECOLOR", &(scalebar->imagecolor));
  writeKeyword(stream, indent, "INTERLACE", scalebar->interlace, 2, MS_TRUE, "TRUE", MS_FALSE, "FALSE");
  writeNumber(stream, indent, "INTERVALS", -1, scalebar->intervals);
  writeLabel(stream, indent, &(scalebar->label));
  writeColor(stream, indent, "OUTLINECOLOR", &(scalebar->outlinecolor));
  if(scalebar->status == MS_EMBED) writeKeyword(stream, indent, "POSITION", scalebar->position, 6, MS_LL, "LL", MS_UL, "UL", MS_UR, "UR", MS_LR, "LR", MS_UC, "UC", MS_LC, "LC");
  writeKeyword(stream, indent, "POSTLABELCACHE", scalebar->postlabelcache, 1, MS_TRUE, "TRUE");
  writeDimension(stream, indent, "SIZE", scalebar->width, scalebar->height, NULL, NULL);
  writeKeyword(stream, indent, "STATUS", scalebar->status, 3, MS_ON, "ON", MS_OFF, "OFF", MS_EMBED, "EMBED");
  writeNumber(stream, indent, "STYLE", 0, scalebar->style);
  writeKeyword(stream, indent, "TRANSPARENT", scalebar->transparent, 2, MS_TRUE, "TRUE", MS_FALSE, "FALSE");
  writeKeyword(stream, indent, "UNITS", scalebar->units, 6, MS_INCHES, "INCHES", MS_FEET ,"FEET", MS_MILES, "MILES", MS_METERS, "METERS", MS_KILOMETERS, "KILOMETERS", MS_NAUTICALMILES, "NAUTICALMILES");
  writeBlockEnd(stream, indent, "SCALEBAR");
  writeLineFeed(stream);
}

/*
** Initialize a queryMapObj structure
*/
void initQueryMap(queryMapObj *querymap)
{
  querymap->width = querymap->height = -1;
  querymap->style = MS_HILITE;
  querymap->status = MS_OFF;
  MS_INIT_COLOR(querymap->color, 255,255,0,255); /* yellow */
}

int loadQueryMap(queryMapObj *querymap)
{
  for(;;) {
    switch(msyylex()) {
    case(QUERYMAP):
      break; /* for string loads */
    case(COLOR):      
      loadColor(&(querymap->color), NULL);
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadQueryMap()");
      return(-1);
    case(END):
      return(0);
      break;
    case(SIZE):
      if(getInteger(&(querymap->width)) == -1) return(-1);
      if(getInteger(&(querymap->height)) == -1) return(-1);
      break;
    case(STATUS):
      if((querymap->status = getSymbol(2, MS_ON,MS_OFF)) == -1) return(-1);
      break;
    case(STYLE):
    case(TYPE):
      if((querymap->style = getSymbol(3, MS_NORMAL,MS_HILITE,MS_SELECTED)) == -1) return(-1);
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadQueryMap()", msyystring_buffer, msyylineno);
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  }
}

int msUpdateQueryMapFromString(queryMapObj *querymap, char *string, int url_string)
{
  if(!querymap || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadQueryMap(querymap) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

static void writeQueryMap(FILE *stream, int indent, queryMapObj *querymap)
{
  indent++;
  writeBlockBegin(stream, indent, "QUERYMAP");
  writeColor(stream, indent, "COLOR", &(querymap->color));
  writeDimension(stream, indent, "SIZE", querymap->width, querymap->height, NULL, NULL);
  writeKeyword(stream, indent, "STATUS", querymap->status, 2, MS_ON, "ON", MS_OFF, "OFF");
  writeKeyword(stream, indent, "STYLE", querymap->style, 3, MS_NORMAL, "NORMAL", MS_HILITE, "HILITE", MS_SELECTED, "SELECTED");
  writeBlockEnd(stream, indent, "QUERYMAP");
  writeLineFeed(stream);
}

/*
** Initialize a webObj structure
*/
void initWeb(webObj *web)
{
  web->extent.minx = web->extent.miny = web->extent.maxx = web->extent.maxy = -1.0;
  web->template = NULL;
  web->header = web->footer = NULL;
  web->error =  web->empty = NULL;
  web->mintemplate = web->maxtemplate = NULL;
  web->minscaledenom = web->maxscaledenom = -1;
  web->log = NULL;
  web->imagepath = msStrdup("");
  web->temppath = NULL;
  web->imageurl = msStrdup("");
  
  initHashTable(&(web->metadata));
  initHashTable(&(web->validation));  

  web->map = NULL;
  web->queryformat = msStrdup("text/html");
  web->legendformat = msStrdup("text/html");
  web->browseformat = msStrdup("text/html");
}

void freeWeb(webObj *web)
{
  msFree(web->template);
  msFree(web->header);
  msFree(web->footer);
  msFree(web->error);
  msFree(web->empty);
  msFree(web->maxtemplate);
  msFree(web->mintemplate);
  msFree(web->log);
  msFree(web->imagepath);
  msFree(web->temppath);
  msFree(web->imageurl);
  msFree(web->queryformat);
  msFree(web->legendformat);
  msFree(web->browseformat);
  if(&(web->metadata)) msFreeHashItems(&(web->metadata));
  if(&(web->validation)) msFreeHashItems(&(web->validation));
}

static void writeWeb(FILE *stream, int indent, webObj *web)
{
  indent++;
  writeBlockBegin(stream, indent, "WEB");
  writeString(stream, indent, "BROWSEFORMAT", "text/html", web->browseformat);
  writeString(stream, indent, "EMPTY", NULL, web->empty);
  writeString(stream, indent, "ERROR", NULL, web->error);
  writeExtent(stream, indent, "EXTENT", web->extent);
  writeString(stream, indent, "FOOTER", NULL, web->footer);
  writeString(stream, indent, "HEADER", NULL, web->header);
  writeString(stream, indent, "IMAGEPATH", "", web->imagepath);
  writeString(stream, indent, "TEMPPATH", NULL, web->temppath);
  writeString(stream, indent, "IMAGEURL", "", web->imageurl);
  writeString(stream, indent, "LEGENDFORMAT", "text/html", web->legendformat);
  writeString(stream, indent, "LOG", NULL, web->log);
  writeNumber(stream, indent, "MAXSCALEDENOM", -1, web->maxscaledenom);
  writeString(stream, indent, "MAXTEMPLATE", NULL, web->maxtemplate);
  writeHashTable(stream, indent, "METADATA", &(web->metadata));
  writeNumber(stream, indent, "MINSCALEDENOM", -1, web->minscaledenom);
  writeString(stream, indent, "MINTEMPLATE", NULL, web->mintemplate);
  writeString(stream, indent, "QUERYFORMAT", "text/html", web->queryformat);
  writeString(stream, indent, "TEMPLATE", NULL, web->template);
  writeHashTable(stream, indent, "VALIDATION", &(web->validation));
  writeBlockEnd(stream, indent, "WEB");
  writeLineFeed(stream);
}

int loadWeb(webObj *web, mapObj *map)
{
  web->map = (mapObj *)map;

  for(;;) {
    switch(msyylex()) {
    case(BROWSEFORMAT): /* change to use validation in 6.0 */
      free(web->browseformat); web->browseformat = NULL; /* there is a default */
      if(getString(&web->browseformat) == MS_FAILURE) return(-1);
      break;
    case(EMPTY):
      if(getString(&web->empty) == MS_FAILURE) return(-1);
      break;
    case(WEB):
      break; /* for string loads */
    case(EOF):
      msSetError(MS_EOFERR, NULL, "loadWeb()");
      return(-1);
    case(END):
      return(0);
      break;
    case(ERROR):
      if(getString(&web->error) == MS_FAILURE) return(-1);
      break;
    case(EXTENT):
      if(getDouble(&(web->extent.minx)) == -1) return(-1);
      if(getDouble(&(web->extent.miny)) == -1) return(-1);
      if(getDouble(&(web->extent.maxx)) == -1) return(-1);
      if(getDouble(&(web->extent.maxy)) == -1) return(-1);
      if (!MS_VALID_EXTENT(web->extent)) {
        msSetError(MS_MISCERR, "Given web extent is invalid. Check that it is in the form: minx, miny, maxx, maxy", "loadWeb()"); 
        return(-1);
      }
      break;
    case(FOOTER):
      if(getString(&web->footer) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(web->footer, msLookupHashTable(&(web->validation), "footer"), map->templatepattern, NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based FOOTER configuration failed pattern validation." , "loadWeb()");
          msFree(web->footer); web->footer=NULL;
          return(-1);
        }
      }
      break;
    case(HEADER):
      if(getString(&web->header) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(web->header, msLookupHashTable(&(web->validation), "header"), map->templatepattern, NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based HEADER configuration failed pattern validation." , "loadWeb()");
          msFree(web->header); web->header=NULL;
          return(-1);
        }
      }
      break;
    case(IMAGEPATH):      
      if(getString(&web->imagepath) == MS_FAILURE) return(-1);
      break;
    case(TEMPPATH):      
      if(getString(&web->temppath) == MS_FAILURE) return(-1);
      break;
    case(IMAGEURL):
      if(getString(&web->imageurl) == MS_FAILURE) return(-1);
      break;
    case(LEGENDFORMAT): /* change to use validation in 6.0 */
      free(web->legendformat); web->legendformat = NULL; /* there is a default */
      if(getString(&web->legendformat) == MS_FAILURE) return(-1);
      break;
    case(LOG):
      if(getString(&web->log) == MS_FAILURE) return(-1);
      break;
    case(MAXSCALE):
    case(MAXSCALEDENOM):
      if(getDouble(&web->maxscaledenom) == -1) return(-1);
      break;
    case(MAXTEMPLATE):
      if(getString(&web->maxtemplate) == MS_FAILURE) return(-1);
      break;
    case(METADATA):
      if(loadHashTable(&(web->metadata)) != MS_SUCCESS) return(-1);
      break;
    case(MINSCALE):
    case(MINSCALEDENOM):
      if(getDouble(&web->minscaledenom) == -1) return(-1);
      break;
    case(MINTEMPLATE):
      if(getString(&web->mintemplate) == MS_FAILURE) return(-1);
      break; 
    case(QUERYFORMAT): /* change to use validation in 6.0 */
      free(web->queryformat); web->queryformat = NULL; /* there is a default */
      if(getString(&web->queryformat) == MS_FAILURE) return(-1);
      break;   
    case(TEMPLATE):
      if(getString(&web->template) == MS_FAILURE) return(-1); /* getString() cleans up previously allocated string */
      if(msyysource == MS_URL_TOKENS) {
        if(msValidateParameter(web->template, msLookupHashTable(&(web->validation), "template"), map->templatepattern, NULL, NULL) != MS_SUCCESS) {
          msSetError(MS_MISCERR, "URL-based TEMPLATE configuration failed pattern validation." , "loadWeb()");
          msFree(web->template); web->template=NULL;
          return(-1);
        }
      }
      break;
    case(VALIDATION):
      if(loadHashTable(&(web->validation)) != MS_SUCCESS) return(-1);
      break;
    default:
      if(strlen(msyystring_buffer) > 0) {
        msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "loadWeb()", msyystring_buffer, msyylineno);
        return(-1);
      } else {
        return(0); /* end of a string, not an error */
      }
    }
  }
}

int msUpdateWebFromString(webObj *web, char *string, int url_string)
{
  if(!web || !string) return MS_FAILURE;

  msAcquireLock( TLOCK_PARSER );

  if(url_string)
    msyystate = MS_TOKENIZE_URL_STRING;
  else
    msyystate = MS_TOKENIZE_STRING;
  msyystring = string;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 */

  if(loadWeb(web, web->map) == -1) {
    msReleaseLock( TLOCK_PARSER );
    return MS_FAILURE; /* parse error */;
  }
  msReleaseLock( TLOCK_PARSER );

  msyylex_destroy();
  return MS_SUCCESS;
}

/*
** Initialize, load and free a mapObj structure
**
** This really belongs in mapobject.c, but currently it also depends on 
** lots of other init methods in this file.
*/
int initMap(mapObj *map)
{
  int i=0;
  MS_REFCNT_INIT(map);

  map->debug = (int)msGetGlobalDebugLevel();

  /* Set maxlayers = 0, layers[] and layerorder[] will be allocated as needed,
   * on the first call to msGrowMapLayers()
   */
  map->numlayers = 0;
  map->maxlayers = 0;
  map->layers = NULL;
  map->layerorder = NULL; /* used to modify the order in which the layers are drawn */

  map->status = MS_ON;
  map->name = msStrdup("MS");
  map->extent.minx = map->extent.miny = map->extent.maxx = map->extent.maxy = -1.0;

  map->scaledenom = -1.0;
  map->resolution = MS_DEFAULT_RESOLUTION; /* pixels per inch */
  map->defresolution = MS_DEFAULT_RESOLUTION; /* pixels per inch */

  map->height = map->width = -1;
  map->maxsize = MS_MAXIMAGESIZE_DEFAULT;

  map->gt.need_geotransform = MS_FALSE;
  map->gt.rotation_angle = 0.0;

  map->units = MS_METERS;
  map->cellsize = 0;
  map->shapepath = NULL;
  map->mappath = NULL;

  MS_INIT_COLOR(map->imagecolor, 255,255,255,255); /* white */

  map->numoutputformats = 0;
  map->outputformatlist = NULL;
  map->outputformat = NULL;

  /* map->configoptions = msCreateHashTable();; */
  initHashTable(&(map->configoptions));
          
  map->imagetype = NULL;

  map->palette.numcolors = 0;

  map->transparent = MS_NOOVERRIDE;
  map->interlace = MS_NOOVERRIDE;
  map->imagequality = MS_NOOVERRIDE;

  for(i=0; i<MS_MAX_LABEL_PRIORITY; i++) {
      map->labelcache.slots[i].labels = NULL; /* cache is initialize at draw time */
      map->labelcache.slots[i].cachesize = 0;
      map->labelcache.slots[i].numlabels = 0;
      map->labelcache.slots[i].markers = NULL;
      map->labelcache.slots[i].markercachesize = 0;
      map->labelcache.slots[i].nummarkers = 0;
  }
  map->labelcache.numlabels = 0;

  map->fontset.filename = NULL;
  map->fontset.numfonts = 0;  
  
  /* map->fontset.fonts = NULL; */
  initHashTable(&(map->fontset.fonts));
  
  msInitSymbolSet(&map->symbolset);
  map->symbolset.fontset =  &(map->fontset);

  initLegend(&map->legend);
  initScalebar(&map->scalebar);
  initWeb(&map->web);
  initReferenceMap(&map->reference);
  initQueryMap(&map->querymap);

#ifdef USE_PROJ
  if(msInitProjection(&(map->projection)) == -1)
    return(-1);
  if(msInitProjection(&(map->latlon)) == -1)
    return(-1);

  /* initialize a default "geographic" projection */
  map->latlon.numargs = 2;
  map->latlon.args[0] = msStrdup("proj=latlong");
  map->latlon.args[1] = msStrdup("ellps=WGS84"); /* probably want a different ellipsoid */
  if(msProcessProjection(&(map->latlon)) == -1) return(-1);
#endif

  map->templatepattern = map->datapattern = NULL;

  /* Encryption key information - see mapcrypto.c */
  map->encryption_key_loaded = MS_FALSE;

  msInitQuery(&(map->query));

  return(0);
}

/*
** Ensure there is at least one free entry in the layers and layerorder 
** arrays of this mapObj. Grow the allocated layers/layerorder arrays if 
** necessary and allocate a new layer for layers[numlayers] if there is 
** not already one, setting its contents to all zero bytes (i.e. does not
** call initLayer() on it).
**
** This function is safe to use for the initial allocation of the layers[]
** and layerorder[] arrays as well (i.e. when maxlayers==0 and layers==NULL)
**
** Returns a reference to the new layerObj on success, NULL on error.
*/
layerObj *msGrowMapLayers( mapObj *map )
{
    /* Do we need to increase the size of layers/layerorder by 
     * MS_LAYER_ALLOCSIZE?
     */
    if (map->numlayers == map->maxlayers) {
        layerObj **newLayersPtr;
        int *newLayerorderPtr;
        int i, newsize;

        newsize = map->maxlayers + MS_LAYER_ALLOCSIZE;

        /* Alloc/realloc layers */
        newLayersPtr = (layerObj**)realloc(map->layers,
                                           newsize*sizeof(layerObj*));
        MS_CHECK_ALLOC(newLayersPtr, newsize*sizeof(layerObj*), NULL);

        map->layers = newLayersPtr;

        /* Alloc/realloc layerorder */
        newLayerorderPtr = (int *)realloc(map->layerorder,
                                          newsize*sizeof(int));
        MS_CHECK_ALLOC(newLayerorderPtr, newsize*sizeof(int), NULL);
        
        map->layerorder = newLayerorderPtr;

        map->maxlayers = newsize;
        for(i=map->numlayers; i<map->maxlayers; i++) {
            map->layers[i] = NULL;
            map->layerorder[i] = 0;
        }
    }

    if (map->layers[map->numlayers]==NULL) {
        map->layers[map->numlayers]=(layerObj*)calloc(1,sizeof(layerObj));
        MS_CHECK_ALLOC(map->layers[map->numlayers], sizeof(layerObj), NULL);
    }

    return map->layers[map->numlayers];
}


int msFreeLabelCacheSlot(labelCacheSlotObj *cacheslot) {
  int i, j;

  /* free the labels */
  if (cacheslot->labels)
    for(i=0; i<cacheslot->numlabels; i++) {
      msFree(cacheslot->labels[i].text);
      if (cacheslot->labels[i].labelpath)
        msFreeLabelPathObj(cacheslot->labels[i].labelpath);
      if( cacheslot->labels[i].label.font != NULL )
          msFree( cacheslot->labels[i].label.font );
      msFreeShape(cacheslot->labels[i].poly); /* empties the shape */
      msFree(cacheslot->labels[i].poly); /* free's the pointer */
      for(j=0;j<cacheslot->labels[i].numstyles; j++) freeStyle(&(cacheslot->labels[i].styles[j]));
      msFree(cacheslot->labels[i].styles);
    }
  msFree(cacheslot->labels);
  cacheslot->labels = NULL;
  cacheslot->cachesize = 0;
  cacheslot->numlabels = 0;
  
  /* free the markers */
  if (cacheslot->markers)
    for(i=0; i<cacheslot->nummarkers; i++) {
      msFreeShape(cacheslot->markers[i].poly);
      msFree(cacheslot->markers[i].poly);
    }
  msFree(cacheslot->markers);
  cacheslot->markers = NULL;
  cacheslot->markercachesize = 0;
  cacheslot->nummarkers = 0;

  return(MS_SUCCESS);
}

int msFreeLabelCache(labelCacheObj *cache) {
  int p;

  for(p=0; p<MS_MAX_LABEL_PRIORITY; p++) {
      if (msFreeLabelCacheSlot(&(cache->slots[p])) != MS_SUCCESS)
          return MS_FAILURE;
  }

  cache->numlabels = 0;

  return MS_SUCCESS;
}

int msInitLabelCacheSlot(labelCacheSlotObj *cacheslot) {

  if(cacheslot->labels || cacheslot->markers) 
      msFreeLabelCacheSlot(cacheslot);

  cacheslot->labels = (labelCacheMemberObj *)malloc(sizeof(labelCacheMemberObj)*MS_LABELCACHEINITSIZE);
  MS_CHECK_ALLOC(cacheslot->labels, sizeof(labelCacheMemberObj)*MS_LABELCACHEINITSIZE, MS_FAILURE);

  cacheslot->cachesize = MS_LABELCACHEINITSIZE;
  cacheslot->numlabels = 0;

  cacheslot->markers = (markerCacheMemberObj *)malloc(sizeof(markerCacheMemberObj)*MS_LABELCACHEINITSIZE);
  MS_CHECK_ALLOC(cacheslot->markers, sizeof(markerCacheMemberObj)*MS_LABELCACHEINITSIZE, MS_FAILURE);

  cacheslot->markercachesize = MS_LABELCACHEINITSIZE;
  cacheslot->nummarkers = 0;

  return(MS_SUCCESS);
}

int msInitLabelCache(labelCacheObj *cache) {
  int p;

  for(p=0; p<MS_MAX_LABEL_PRIORITY; p++) {
      if (msInitLabelCacheSlot(&(cache->slots[p])) != MS_SUCCESS)
          return MS_FAILURE;
  }
  cache->numlabels = 0;

  return MS_SUCCESS;
}


int msSaveMap(mapObj *map, char *filename)
{
  int i, indent=0;
  FILE *stream;
  char szPath[MS_MAXPATHLEN];

  if(!map) {
    msSetError(MS_MISCERR, "Map is undefined.", "msSaveMap()");
    return(-1);
  }

  if(!filename) {
    msSetError(MS_MISCERR, "Filename is undefined.", "msSaveMap()");
    return(-1);
  }

  stream = fopen(msBuildPath(szPath, map->mappath, filename), "w");
  if(!stream) {
    msSetError(MS_IOERR, "(%s)", "msSaveMap()", filename);    
    return(-1);
  }

  writeBlockBegin(stream, indent, "MAP");
  writeHashTableInline(stream, indent, "CONFIG", &(map->configoptions));
  writeString(stream, indent, "DATAPATTERN", NULL, map->datapattern); /* depricated */
  writeNumber(stream, indent, "DEBUG", 0, map->debug);
  writeNumber(stream, indent, "DEFRESOLUTION", 72.0, map->defresolution);
  writeExtent(stream, indent, "EXTENT", map->extent);
  writeString(stream, indent, "FONTSET", NULL, map->fontset.filename);
  writeColor(stream, indent, "IMAGECOLOR", &(map->imagecolor));
  writeString(stream, indent, "IMAGETYPE", NULL, map->imagetype);
  writeKeyword(stream, indent, "INTERLACE", map->interlace, 2, MS_TRUE, "TRUE", MS_FALSE, "FALSE");
  writeNumber(stream, indent, "MAXSIZE", MS_MAXIMAGESIZE_DEFAULT, map->maxsize);
  writeString(stream, indent, "NAME", NULL, map->name);
  writeNumber(stream, indent, "RESOLUTION", 72.0, map->resolution);
  writeString(stream, indent, "SHAPEPATH", NULL, map->shapepath);   
  writeDimension(stream, indent, "SIZE", map->width, map->height, NULL, NULL);
  writeKeyword(stream, indent, "STATUS", map->status, 2, MS_ON, "ON", MS_OFF, "OFF");
  writeString(stream, indent, "SYMBOLSET", NULL, map->symbolset.filename);
  writeString(stream, indent, "TEMPLATEPATTERN", NULL, map->templatepattern); /* depricated */
  writeKeyword(stream, indent, "TRANSPARENT", map->transparent, 2, MS_TRUE, "TRUE", MS_FALSE, "FALSE");
  writeKeyword(stream, indent, "UNITS", map->units, 7, MS_INCHES, "INCHES", MS_FEET ,"FEET", MS_MILES, "MILES", MS_METERS, "METERS", MS_KILOMETERS, "KILOMETERS", MS_NAUTICALMILES, "NAUTICALMILES", MS_DD, "DD"); 
  writeLineFeed(stream); 

  writeOutputformat(stream, indent, map);

  /* write symbol with INLINE tag in mapfile */
  for(i=0; i<map->symbolset.numsymbols; i++) {
    writeSymbol(map->symbolset.symbol[i], stream);
  }

  writeProjection(stream, indent, &(map->projection));
  
  writeLegend(stream, indent, &(map->legend));
  writeQueryMap(stream, indent, &(map->querymap));
  writeReferenceMap(stream, indent, &(map->reference));
  writeScalebar(stream, indent, &(map->scalebar));
  writeWeb(stream, indent, &(map->web));

  for(i=0; i<map->numlayers; i++)
    writeLayer(stream, indent, GET_LAYER(map, map->layerorder[i]));

  writeBlockEnd(stream, indent, "MAP");
  fclose(stream);

  return(0);
}

static int loadMapInternal(mapObj *map)
{
  int i,j,k;
  int foundMapToken=MS_FALSE; 
  int token; 

  for(;;) {

    token = msyylex(); 

    if(!foundMapToken && token != MAP) { 
      msSetError(MS_IDENTERR, "First token must be MAP, this doesn't look like a mapfile.", "msLoadMap()"); 
      return(MS_FAILURE); 
    }

    switch(token) {

    case(CONFIG):
    {
        char *key=NULL, *value=NULL;

        if( getString(&key) == MS_FAILURE )
            return MS_FAILURE;

        if( getString(&value) == MS_FAILURE )
            return MS_FAILURE;

        if (msSetConfigOption( map, key, value ) == MS_FAILURE)
            return MS_FAILURE;

        free( key ); key=NULL;
        free( value ); value=NULL;
    }
    break;

    case(DATAPATTERN):
      if(getString(&map->datapattern) == MS_FAILURE) return MS_FAILURE;
      break;
    case(DEBUG):
      if((map->debug = getSymbol(3, MS_ON,MS_OFF, MS_NUMBER)) == -1) return MS_FAILURE;
      if(map->debug == MS_NUMBER) map->debug = (int) msyynumber;
      break;
    case(END):
      if(msyyin) {
          fclose(msyyin);
          msyyin = NULL;
      }

      /*** Make config options current ***/
      msApplyMapConfigOptions( map );

      /*** Compute rotated extent info if applicable ***/
      msMapComputeGeotransform( map );
                                            
      /*** OUTPUTFORMAT related setup ***/
      if( msPostMapParseOutputFormatSetup( map ) == MS_FAILURE )
          return MS_FAILURE;

      if(loadSymbolSet(&(map->symbolset), map) == -1) return MS_FAILURE;

      if (msResolveSymbolNames(map) == MS_FAILURE) return MS_FAILURE;
      
      /*backwards compatibility symbol to style merging*/
      for(i=0; i<map->numlayers; i++) {
        layerObj *layer = GET_LAYER(map, i);
        for(j=0; j<layer->numclasses; j++) {
          classObj *class = layer->class[j];
          for(k=0; k<class->numstyles; k++) {
            styleObj *style = class->styles[k];
            if(style->symbol != 0) {
              symbolObj *symbol = map->symbolset.symbol[style->symbol];
              if (symbol)
              {
                  if(style->gap == 0)
                    style->gap = symbol->gap;
                  if(style->patternlength == 0) {
                      int idx;
                      style->patternlength = symbol->patternlength;
                      for(idx=0;idx<style->patternlength;idx++)
                        style->pattern[idx] = (double)(symbol->pattern[idx]);
                  }
                  if(style->position == MS_CC)
                    style->position = symbol->position;
                  if(style->linecap == MS_CJC_ROUND)
                    style->linecap = symbol->linecap;
                  if(style->linejoin == MS_CJC_NONE)
                    style->linejoin = symbol->linejoin;
                  if(style->linejoinmaxsize == 3)
                    style->linejoinmaxsize = symbol->linejoinmaxsize;
              }
            }
          }
        }
      }
      
      

#if defined (USE_GD_TTF) || defined (USE_GD_FT)
      if(msLoadFontSet(&(map->fontset), map) == -1) return MS_FAILURE;
#endif

      return MS_SUCCESS;
      break;
    case(EOF):
      msSetError(MS_EOFERR, NULL, "msLoadMap()");
      return MS_FAILURE;
    case(EXTENT):
    {
       if(getDouble(&(map->extent.minx)) == -1) return MS_FAILURE;
       if(getDouble(&(map->extent.miny)) == -1) return MS_FAILURE;
       if(getDouble(&(map->extent.maxx)) == -1) return MS_FAILURE;
       if(getDouble(&(map->extent.maxy)) == -1) return MS_FAILURE;
       if (!MS_VALID_EXTENT(map->extent)) {
    	 msSetError(MS_MISCERR, "Given map extent is invalid. Check that it " \
        "is in the form: minx, miny, maxx, maxy", "loadMapInternal()"); 
      	 return MS_FAILURE;
       }
    }
    break;
    case(ANGLE):
    {
        double rotation_angle;
        if(getDouble(&(rotation_angle)) == -1) return MS_FAILURE;
        msMapSetRotation( map, rotation_angle );
    }
    break;
    case(TEMPLATEPATTERN):
      if(getString(&map->templatepattern) == MS_FAILURE) return MS_FAILURE;
      break;
    case(FONTSET):
      if(getString(&map->fontset.filename) == MS_FAILURE) return MS_FAILURE;
      break;
    case(IMAGECOLOR):
      if(loadColor(&(map->imagecolor), NULL) != MS_SUCCESS) return MS_FAILURE;
      break; 
    case(IMAGEQUALITY):
      if(getInteger(&(map->imagequality)) == -1) return MS_FAILURE;
      break;
    case(IMAGETYPE):
      map->imagetype = getToken();
      break;
    case(INTERLACE):
      if((map->interlace = getSymbol(2, MS_ON,MS_OFF)) == -1) return MS_FAILURE;
      break;
    case(LATLON):
      msFreeProjection(&map->latlon);
      if(loadProjection(&map->latlon) == -1) return MS_FAILURE;
      break;
    case(LAYER):
      if(msGrowMapLayers(map) == NULL)
          return MS_FAILURE;
      if(initLayer((GET_LAYER(map, map->numlayers)), map) == -1) return MS_FAILURE;
      if(loadLayer((GET_LAYER(map, map->numlayers)), map) == -1) return MS_FAILURE;
      GET_LAYER(map, map->numlayers)->index = map->numlayers; /* save the index */
      /* Update the layer order list with the layer's index. */
      map->layerorder[map->numlayers] = map->numlayers;
      map->numlayers++;
      break;
    case(OUTPUTFORMAT):
      if(loadOutputFormat(map) == -1) return MS_FAILURE;
      break;
    case(LEGEND):
      if(loadLegend(&(map->legend), map) == -1) return MS_FAILURE;
      break;
    case(MAP):
      foundMapToken = MS_TRUE;
      break;   
    case(MAXSIZE):
      if(getInteger(&(map->maxsize)) == -1) return MS_FAILURE;
      break;
    case(NAME):
      free(map->name); map->name = NULL; /* erase default */
      if(getString(&map->name) == MS_FAILURE) return MS_FAILURE;
      break;
    case(PROJECTION):
      if(loadProjection(&map->projection) == -1) return MS_FAILURE;
      break;
    case(QUERYMAP):
      if(loadQueryMap(&(map->querymap)) == -1) return MS_FAILURE;
      break;
    case(REFERENCE):
      if(loadReferenceMap(&(map->reference), map) == -1) return MS_FAILURE;
      break;
    case(RESOLUTION):
      if(getDouble(&(map->resolution)) == -1) return MS_FAILURE;
      break;
    case(DEFRESOLUTION):
      if(getDouble(&(map->defresolution)) == -1) return MS_FAILURE;
      break;
    case(SCALE):
    case(SCALEDENOM):
      if(getDouble(&(map->scaledenom)) == -1) return MS_FAILURE;
      break;
    case(SCALEBAR):
      if(loadScalebar(&(map->scalebar)) == -1) return MS_FAILURE;
      break;   
    case(SHAPEPATH):
      if(getString(&map->shapepath) == MS_FAILURE) return MS_FAILURE;
      break;
    case(SIZE):
      if(getInteger(&(map->width)) == -1) return MS_FAILURE;
      if(getInteger(&(map->height)) == -1) return MS_FAILURE;
      break;
    case(STATUS):
      if((map->status = getSymbol(2, MS_ON,MS_OFF)) == -1) return MS_FAILURE;
      break;
    case(SYMBOL):
      if(msGrowSymbolSet(&(map->symbolset)) == NULL)
          return MS_FAILURE;
      if((loadSymbol(map->symbolset.symbol[map->symbolset.numsymbols], map->mappath) == -1)) return MS_FAILURE;
      map->symbolset.symbol[map->symbolset.numsymbols]->inmapfile = MS_TRUE;
      map->symbolset.numsymbols++;
      break;
    case(SYMBOLSET):
      if(getString(&map->symbolset.filename) == MS_FAILURE) return MS_FAILURE;
      break;
    case(TRANSPARENT):
      if((map->transparent = getSymbol(2, MS_ON,MS_OFF)) == -1) return MS_FAILURE;
      break;
    case(UNITS):
      if((map->units = getSymbol(7, MS_INCHES,MS_FEET,MS_MILES,MS_METERS,MS_KILOMETERS,MS_NAUTICALMILES,MS_DD)) == -1) return MS_FAILURE;
      break;
    case(WEB):
      if(loadWeb(&(map->web), map) == -1) return MS_FAILURE;
      break;
    default:
      msSetError(MS_IDENTERR, "Parsing error near (%s):(line %d)", "msLoadMap()", msyystring_buffer, msyylineno);
      return MS_FAILURE;
    }
  } /* next token */
}

/*
** Sets up string-based mapfile loading and calls loadMapInternal to do the work.
*/
mapObj *msLoadMapFromString(char *buffer, char *new_mappath)
{
  mapObj *map;
  struct mstimeval starttime, endtime;
  char szPath[MS_MAXPATHLEN], szCWDPath[MS_MAXPATHLEN];
  char *mappath=NULL;
  int debuglevel;

  debuglevel = (int)msGetGlobalDebugLevel();

  if (debuglevel >= MS_DEBUGLEVEL_TUNING)
  {
      /* In debug mode, track time spent loading/parsing mapfile. */
      msGettimeofday(&starttime, NULL);
  }

  if(!buffer) {
    msSetError(MS_MISCERR, "No buffer to load.", "msLoadMapFromString()");
    return(NULL);
  }

  /*
  ** Allocate mapObj structure
  */
  map = (mapObj *)calloc(sizeof(mapObj),1);
  MS_CHECK_ALLOC(map, sizeof(mapObj), NULL);

  if(initMap(map) == -1) { /* initialize this map */
    msFree(map);
    return(NULL);
  }

  msAcquireLock( TLOCK_PARSER ); /* might need to move this lock a bit higher, yup (bug 2108) */

  msyystate = MS_TOKENIZE_STRING;
  msyystring = buffer;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyylineno = 1; /* start at line 1 (do lines mean anything here?) */

  /* If new_mappath is provided then use it, otherwise use the CWD */
  getcwd(szCWDPath, MS_MAXPATHLEN);
  if (new_mappath) {
    mappath = msStrdup(new_mappath);
    map->mappath = msStrdup(msBuildPath(szPath, szCWDPath, mappath));
  } else
    map->mappath = msStrdup(szCWDPath);

  msyybasepath = map->mappath; /* for INCLUDEs */

  if(loadMapInternal(map) != MS_SUCCESS) {
    msFreeMap(map);
    msReleaseLock( TLOCK_PARSER );
    if(mappath != NULL) free(mappath);
    return NULL;
  }
  msReleaseLock( TLOCK_PARSER );

  if (debuglevel >= MS_DEBUGLEVEL_TUNING)
  {
      /* In debug mode, report time spent loading/parsing mapfile. */
      msGettimeofday(&endtime, NULL);
      msDebug("msLoadMap(): %.3fs\n", 
              (endtime.tv_sec+endtime.tv_usec/1.0e6)-
              (starttime.tv_sec+starttime.tv_usec/1.0e6) );
  }

  if (mappath != NULL) free(mappath);
  msyylex_destroy();

  if (msResolveSymbolNames(map) == MS_FAILURE) return NULL;

  return map;
}

/*
** Sets up file-based mapfile loading and calls loadMapInternal to do the work.
*/
mapObj *msLoadMap(char *filename, char *new_mappath)
{
  mapObj *map;
  struct mstimeval starttime, endtime;
  char szPath[MS_MAXPATHLEN], szCWDPath[MS_MAXPATHLEN];
  int debuglevel;

  debuglevel = (int)msGetGlobalDebugLevel();

  if (debuglevel >= MS_DEBUGLEVEL_TUNING)
  {
      /* In debug mode, track time spent loading/parsing mapfile. */
      msGettimeofday(&starttime, NULL);
  }

  if(!filename) {
    msSetError(MS_MISCERR, "Filename is undefined.", "msLoadMap()");
    return(NULL);
  }
  
  if(getenv("MS_MAPFILE_PATTERN")) { /* user override */
    if(msEvalRegex(getenv("MS_MAPFILE_PATTERN"), filename) != MS_TRUE) {
      msSetError(MS_REGEXERR, "MS_MAPFILE_PATTERN validation failed." , "msLoadMap()");
      return(NULL);
    }
  } else { /* check the default */
    if(msEvalRegex(MS_DEFAULT_MAPFILE_PATTERN, filename) != MS_TRUE) {
      msSetError(MS_REGEXERR, "MS_DEFAULT_MAPFILE_PATTERN validation failed." , "msLoadMap()");
      return(NULL);
    }
  }
 
  /*
  ** Allocate mapObj structure
  */
  map = (mapObj *)calloc(sizeof(mapObj),1);
  MS_CHECK_ALLOC(map, sizeof(mapObj), NULL);

  if(initMap(map) == -1) { /* initialize this map */
    msFree(map);
    return(NULL);
  }
  
  msAcquireLock( TLOCK_PARSER );  /* Steve: might need to move this lock a bit higher; Umberto: done */

#ifdef USE_XMLMAPFILE
  /* If the mapfile is an xml mapfile, transform it */
  if ((getenv("MS_XMLMAPFILE_XSLT")) && 
      (msEvalRegex(MS_DEFAULT_XMLMAPFILE_PATTERN, filename) == MS_TRUE)) {

      msyyin = tmpfile();
      if (msyyin == NULL)
      {
          msSetError(MS_IOERR, "tmpfile() failed to create temporary file", "msLoadMap()");
          msReleaseLock( TLOCK_PARSER );
      }
      
      if (msTransformXmlMapfile(getenv("MS_XMLMAPFILE_XSLT"), filename, msyyin) != MS_SUCCESS)
      {
          fclose(msyyin);
          return NULL;
      }
      fseek ( msyyin , 0 , SEEK_SET );
  }
  else
  {
#endif
      if((msyyin = fopen(filename,"r")) == NULL) {
          msSetError(MS_IOERR, "(%s)", "msLoadMap()", filename);
          msReleaseLock( TLOCK_PARSER );
          return NULL;
      }
#ifdef USE_XMLMAPFILE
  }
#endif

  msyystate = MS_TOKENIZE_FILE;
  msyylex(); /* sets things up, but doesn't process any tokens */

  msyyrestart(msyyin); /* start at line begining, line 1 */
  msyylineno = 1;

  /* If new_mappath is provided then use it, otherwise use the location */
  /* of the mapfile as the default path */
  getcwd(szCWDPath, MS_MAXPATHLEN);
  if (new_mappath)
    map->mappath = msStrdup(msBuildPath(szPath, szCWDPath, msStrdup(new_mappath)));
  else {
    char *path = msGetPath(filename);
    map->mappath = msStrdup(msBuildPath(szPath, szCWDPath, path));
    if( path ) free( path );
  }

  msyybasepath = map->mappath; /* for INCLUDEs */

  if(loadMapInternal(map) != MS_SUCCESS) {
    msFreeMap(map);
    msReleaseLock( TLOCK_PARSER );
    if( msyyin ) {
        fclose(msyyin);
        msyyin = NULL;
    }
    return NULL;
  }
  msReleaseLock( TLOCK_PARSER );

  if (debuglevel >= MS_DEBUGLEVEL_TUNING)
  {
      /* In debug mode, report time spent loading/parsing mapfile. */
      msGettimeofday(&endtime, NULL);
      msDebug("msLoadMap(): %.3fs\n", 
              (endtime.tv_sec+endtime.tv_usec/1.0e6)-
              (starttime.tv_sec+starttime.tv_usec/1.0e6) );
  }

  return map;
}

/*
** Loads mapfile snippets via a URL (only via the CGI so don't worry about thread locks)
*/
int msUpdateMapFromURL(mapObj *map, char *variable, char *string)
{
  int i, j, k, s;
  errorObj *ms_error;

  /* make sure this configuration can be modified */
  if(msLookupHashTable(&(map->web.validation), "immutable"))
    return(MS_SUCCESS); /* fail silently */

  msyystate = MS_TOKENIZE_URL_VARIABLE; /* set lexer state and input to tokenize */
  msyystring = variable;
  msyylineno = 1;

  ms_error = msGetErrorObj();
  ms_error->code = MS_NOERR; /* init error code */

  switch(msyylex()) {
  case(MAP):
    switch(msyylex()) {
    case(CONFIG): {
        char *key=NULL, *value=NULL;
        if((getString(&key) != MS_FAILURE) && (getString(&value) != MS_FAILURE)) {
          msSetConfigOption( map, key, value );
          free( key ); key=NULL;
          free( value ); value=NULL;
        }
      } break;
    case(EXTENT):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();

      if(getDouble(&(map->extent.minx)) == -1) break;
      if(getDouble(&(map->extent.miny)) == -1) break;
      if(getDouble(&(map->extent.maxx)) == -1) break;
      if(getDouble(&(map->extent.maxy)) == -1) break;
      if (!MS_VALID_EXTENT(map->extent)) {
        msSetError(MS_MISCERR, "Given map extent is invalid. Check that it is in the form: minx, miny, maxx, maxy", "msLoadMapParameterFromUrl()"); 
        break;
      }
      msMapComputeGeotransform( map );
      break;
    case(ANGLE): {
        double rotation_angle;
        msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
        msyylex();
      
        if(getDouble(&(rotation_angle)) == -1) break;
        msMapSetRotation( map, rotation_angle );
      } break;
    case(IMAGECOLOR):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();
      
      if(loadColor(&(map->imagecolor), NULL) != MS_SUCCESS) break;
      break;
    case(IMAGETYPE):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();

      /* TODO: should validate or does msPostMapParseOutputFormatSetup() do enough? */

      map->imagetype = getToken();
      msPostMapParseOutputFormatSetup( map );
      break;
    case(LAYER):
      if((s = getSymbol(2, MS_NUMBER, MS_STRING)) == -1) {
        return MS_FAILURE;
      }
      if(s == MS_STRING)
        i = msGetLayerIndex(map, msyystring_buffer);
      else
        i = (int) msyynumber;

      if(i>=map->numlayers || i<0) {
        msSetError(MS_MISCERR, "Layer to be modified not valid.", "msUpdateMapFromURL()");
        return MS_FAILURE;
      }

      /* make sure this layer can be modified */
      if(msLookupHashTable(&(GET_LAYER(map, i)->validation), "immutable"))
        return(MS_SUCCESS); /* fail silently */

      if(msyylex() == CLASS) {
        if((s = getSymbol(2, MS_NUMBER, MS_STRING)) == -1) return MS_FAILURE;
        if(s == MS_STRING)
          j = msGetClassIndex(GET_LAYER(map, i), msyystring_buffer);
        else
          j = (int) msyynumber;

        if(j>=GET_LAYER(map, i)->numclasses || j<0) {
          msSetError(MS_MISCERR, "Class to be modified not valid.", "msUpdateMapFromURL()");
          return MS_FAILURE;
        }

	/* make sure this class can be modified */
	if(msLookupHashTable(&(GET_LAYER(map, i)->class[j]->validation), "immutable"))
	  return(MS_SUCCESS); /* fail silently */

        if(msyylex() == STYLE) {
          if(getInteger(&k) == -1) return MS_FAILURE;

          if(k>=GET_LAYER(map, i)->class[j]->numstyles || k<0) {
            msSetError(MS_MISCERR, "Style to be modified not valid.", "msUpdateMapFromURL()");
            return MS_FAILURE;
          }

          if(msUpdateStyleFromString((GET_LAYER(map, i))->class[j]->styles[k], string, MS_TRUE) != MS_SUCCESS) return MS_FAILURE;
        } else {
          if(msUpdateClassFromString((GET_LAYER(map, i))->class[j], string, MS_TRUE) != MS_SUCCESS) return MS_FAILURE;
	}
      } else {
        if(msUpdateLayerFromString((GET_LAYER(map, i)), string, MS_TRUE) != MS_SUCCESS) return MS_FAILURE;
      }

      /* make sure any symbol names for this layer have been resolved (bug #2700) */
      for(j=0; j<GET_LAYER(map, i)->numclasses; j++) {
        for(k=0; k<GET_LAYER(map, i)->class[j]->numstyles; k++) {
          if(GET_LAYER(map, i)->class[j]->styles[k]->symbolname && GET_LAYER(map, i)->class[j]->styles[k]->symbol == 0) {
            if((GET_LAYER(map, i)->class[j]->styles[k]->symbol =  msGetSymbolIndex(&(map->symbolset), GET_LAYER(map, i)->class[j]->styles[k]->symbolname, MS_TRUE)) == -1) {
              msSetError(MS_MISCERR, "Undefined symbol \"%s\" in class %d, style %d of layer %s.", "msUpdateMapFromURL()", GET_LAYER(map, i)->class[j]->styles[k]->symbolname, j, k, GET_LAYER(map, i)->name);
              return MS_FAILURE;
            }
          }
          if(!MS_IS_VALID_ARRAY_INDEX(GET_LAYER(map, i)->class[j]->styles[k]->symbol, map->symbolset.numsymbols)) {
            msSetError(MS_MISCERR, "Invalid symbol index in class %d, style %d of layer %s.", "msUpdateMapFromURL()", j, k, GET_LAYER(map, i)->name);
            return MS_FAILURE;
          }
        }
      }

      break;
    case(LEGEND):
      return msUpdateLegendFromString(&(map->legend), string, MS_TRUE);
    case(PROJECTION):
      msLoadProjectionString(&(map->projection), string);
      break;
    case(QUERYMAP):
      return msUpdateQueryMapFromString(&(map->querymap), string, MS_TRUE);
    case(REFERENCE):
      return msUpdateReferenceMapFromString(&(map->reference), string, MS_TRUE);
    case(RESOLUTION):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();

      if(getDouble(&(map->resolution)) == -1) break;      
      break;
    case(DEFRESOLUTION):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();

      if(getDouble(&(map->defresolution)) == -1) break;      
      break;    case(SCALEBAR):
      return msUpdateScalebarFromString(&(map->scalebar), string, MS_TRUE);      
    case(SIZE):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();

      if(getInteger(&(map->width)) == -1) break;
      if(getInteger(&(map->height)) == -1) break;

      if(map->width > map->maxsize || map->height > map->maxsize || map->width < 0 || map->height < 0) {
        msSetError(MS_WEBERR, "Image size out of range.", "msUpdateMapFromURL()");
        break;
      }
      msMapComputeGeotransform( map );
      break;
    case(TRANSPARENT):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();

      if((map->transparent = getSymbol(2, MS_ON,MS_OFF)) == -1) break;
      msPostMapParseOutputFormatSetup( map );
      break;
    case(UNITS):
      msyystate = MS_TOKENIZE_URL_STRING; msyystring = string;
      msyylex();

      if((map->units = getSymbol(7, MS_INCHES,MS_FEET,MS_MILES,MS_METERS,MS_KILOMETERS,MS_NAUTICALMILES,MS_DD)) == -1) break;
      break;
    case(WEB):
      return msUpdateWebFromString(&(map->web), string, MS_TRUE);
    default:
      break; /* malformed string */
    }
    break;
  default:
    break;
  }

  /* msyystate = 3; */ /* restore lexer state */
  /* msyylex(); */

  if(ms_error->code != MS_NOERR) return(MS_FAILURE);

  return(MS_SUCCESS);
}

static int layerNeedsSubstitutions(layerObj *layer, char *from) {
  int i;

  if(layer->data && (strcasestr(layer->data, from) != NULL)) return MS_TRUE;
  if(layer->tileindex && (strcasestr(layer->tileindex, from) != NULL)) return MS_TRUE;
  if(layer->connection && (strcasestr(layer->connection, from) != NULL)) return MS_TRUE;
  if(layer->filter.string && (strcasestr(layer->filter.string, from) != NULL)) return MS_TRUE;

  for(i=0; i<layer->numclasses; i++) {
    if(layer->class[i]->expression.string && (strcasestr(layer->class[i]->expression.string, from) != NULL)) 
      return MS_TRUE;
  }

  if(!msHashIsEmpty(&layer->bindvals)) return MS_TRUE;

  return MS_FALSE;
}

static void layerSubstituteString(layerObj *layer, char *from, char *to) {
  int i;
  char *bindvals_key, *bindvals_val;

  if(layer->data) layer->data = msCaseReplaceSubstring(layer->data, from, to);
  if(layer->tileindex) layer->tileindex = msCaseReplaceSubstring(layer->tileindex, from, to);
  if(layer->connection) layer->connection = msCaseReplaceSubstring(layer->connection, from, to);
  if(layer->filter.string) layer->filter.string = msCaseReplaceSubstring(layer->filter.string, from, to);

  for(i=0; i<layer->numclasses; i++) {
    if(layer->class[i]->expression.string)
      layer->class[i]->expression.string = msCaseReplaceSubstring(layer->class[i]->expression.string, from, to);
  }

  /* The bindvalues are most useful when able to substitute values from the URL */
  bindvals_key = (char*)msFirstKeyFromHashTable(&layer->bindvals);
  while(bindvals_key != NULL) {
    bindvals_val = msStrdup((char*)msLookupHashTable(&layer->bindvals, bindvals_key));
    msInsertHashTable(&layer->bindvals, bindvals_key, msCaseReplaceSubstring(bindvals_val, from, to));
    bindvals_key = (char*)msNextKeyFromHashTable(&layer->bindvals, bindvals_key);
  }
}

/* 
** Loop through layer metadata for keys that have a default_%key% pattern to replace
** remaining %key% entries by their default value. 
*/
void msApplyDefaultSubstitutions(mapObj *map) {
  int i;
  for(i=0;i<map->numlayers;i++) {
    layerObj *layer = GET_LAYER(map, i);
    const char *defaultkey = msFirstKeyFromHashTable(&(layer->metadata));
    while(defaultkey) {
      if(!strncmp(defaultkey,"default_",8)){
          size_t buffer_size = (strlen(defaultkey)-5);
        char *tmpstr = (char *)msSmallMalloc(buffer_size);
        snprintf(tmpstr, buffer_size, "%%%s%%", &(defaultkey[8]));

        layerSubstituteString(layer,tmpstr,msLookupHashTable(&(layer->metadata),defaultkey));
        free(tmpstr);
      }
      defaultkey = msNextKeyFromHashTable(&(layer->metadata),defaultkey);
    }
  }
}

void msApplySubstitutions(mapObj *map, char **names, char **values, int npairs) {
  int i,j;

  char *tag=NULL;
  char *validation_pattern_key=NULL;

  for(i=0; i<npairs; i++) {

    /* runtime subtitution string */
    tag = (char *) msSmallMalloc(sizeof(char)*strlen(names[i]) + 3);
    sprintf(tag, "%%%s%%", names[i]);

    /* validation pattern key - depricated */
    validation_pattern_key = (char *) msSmallMalloc(sizeof(char)*strlen(names[i]) + 20);
    sprintf(validation_pattern_key,"%s_validation_pattern", names[i]);

    for(j=0; j<map->numlayers; j++) {
      layerObj *layer = GET_LAYER(map, j);

      if(!layerNeedsSubstitutions(layer, tag)) continue;

      fprintf(stderr, "Layer %s, tag %s...\n", layer->name, tag);

      if(msValidateParameter(values[i], msLookupHashTable(&(layer->validation), names[i]), msLookupHashTable(&(layer->metadata), validation_pattern_key), msLookupHashTable(&(map->web.validation), names[i]), NULL) == MS_SUCCESS) {
        layerSubstituteString(layer, tag, values[i]);
      }
    }

    msFree(tag);
    msFree(validation_pattern_key);
  } /* next name/value pair */
}

/*
** Returns an array with one entry per mapfile token.  Useful to manipulate
** mapfiles in MapScript.
**
** The returned array should be freed using msFreeCharArray().
*/
static char **tokenizeMapInternal(char *filename, int *ret_numtokens)
{
  char **tokens = NULL;
  int  numtokens=0, numtokens_allocated=0;
  size_t buffer_size = 0;

  *ret_numtokens = 0;

  if(!filename) {
    msSetError(MS_MISCERR, "Filename is undefined.", "msTokenizeMap()");
    return NULL;
  }
  
  /*
  ** Check map filename to make sure it's legal
  */
  if(getenv("MS_MAPFILE_PATTERN")) { /* user override */
    if(msEvalRegex(getenv("MS_MAPFILE_PATTERN"), filename) != MS_TRUE) {
      msSetError(MS_REGEXERR, "MS_MAPFILE_PATTERN validation failed." , "msLoadMap()");
      return(NULL);
    }
  } else { /* check the default */
    if(msEvalRegex(MS_DEFAULT_MAPFILE_PATTERN, filename) != MS_TRUE) {
      msSetError(MS_REGEXERR, "MS_DEFAULT_MAPFILE_PATTERN validation failed." , "msLoadMap()");
      return(NULL);
    }
  }

  if((msyyin = fopen(filename,"r")) == NULL) {
    msSetError(MS_IOERR, "(%s)", "msTokenizeMap()", filename);
    return NULL;
  }

  msyystate = MS_TOKENIZE_FILE; /* restore lexer state to INITIAL, and do return comments */
  msyylex();
  msyyreturncomments = 1; /* want all tokens, including comments */

  msyyrestart(msyyin); /* start at line begining, line 1 */
  msyylineno = 1;

  numtokens = 0;
  numtokens_allocated = 256; 
  tokens = (char **) malloc(numtokens_allocated*sizeof(char*));
  if(tokens == NULL) {
    msSetError(MS_MEMERR, NULL, "msTokenizeMap()");
    fclose(msyyin);
    return NULL;
  }

  for(;;) {

    if(numtokens_allocated <= numtokens) {        
      numtokens_allocated *= 2; /* double size of the array every time we reach the limit */
      tokens = (char **)realloc(tokens, numtokens_allocated*sizeof(char*));
      if(tokens == NULL) {
        msSetError(MS_MEMERR, "Realloc() error.", "msTokenizeMap()");
        fclose(msyyin);
        return NULL;
      }
    }
 
    switch(msyylex()) {   
      case(EOF): /* This is the normal way out... cleanup and exit */        
        fclose(msyyin);      
        *ret_numtokens = numtokens;
        return(tokens);
        break;
      case(MS_STRING):
        buffer_size = strlen(msyystring_buffer)+2+1;
        tokens[numtokens] = (char*) msSmallMalloc(buffer_size);
        snprintf(tokens[numtokens], buffer_size, "\"%s\"", msyystring_buffer);
        break;
      case(MS_EXPRESSION):
        buffer_size = strlen(msyystring_buffer)+2+1;
        tokens[numtokens] = (char*) msSmallMalloc(buffer_size);
        snprintf(tokens[numtokens], buffer_size, "(%s)", msyystring_buffer);
        break;
      case(MS_REGEX):
        buffer_size = strlen(msyystring_buffer)+2+1;
        tokens[numtokens] = (char*) msSmallMalloc(buffer_size);
        snprintf(tokens[numtokens], buffer_size, "/%s/", msyystring_buffer);
        break;
      default:
        tokens[numtokens] = msStrdup(msyystring_buffer);
        break;
    }

    if(tokens[numtokens] == NULL) {
      msSetError(MS_MEMERR, NULL, "msTokenizeMap()");
      fclose(msyyin);
      return NULL;
    }

    numtokens++;
  }

  return NULL;   /* should never get here */
}

/*
** Wraps tokenizeMapInternal
*/
char **msTokenizeMap(char *filename, int *numtokens)
{
  char **tokens;

  msAcquireLock( TLOCK_PARSER );
  tokens = tokenizeMapInternal( filename, numtokens );
  msReleaseLock( TLOCK_PARSER );

  return tokens;
}

void msCloseConnections(mapObj *map) {
  int i;
  layerObj *lp;

  for (i=0;i<map->numlayers;i++) {
    lp = (GET_LAYER(map, i));

    /* If the vtable is null, then the layer is never accessed or used -> skip it
     */
    if (lp->vtable) {
        lp->vtable->LayerCloseConnection(lp);
    }
  }
}

void initResultCache(resultCacheObj *resultcache)
{
    if (resultcache)
    {
        resultcache->results = NULL;
        resultcache->numresults = 0;
        resultcache->cachesize = 0;
        resultcache->bounds.minx = resultcache->bounds.miny = resultcache->bounds.maxx = resultcache->bounds.maxy = -1;
        resultcache->usegetshape = MS_FALSE;
    }
}

static int msResolveSymbolNames(mapObj* map)
{
    int i, j, k;
    /* step through layers and classes to resolve symbol names */
    for(i=0; i<map->numlayers; i++) {
        for(j=0; j<GET_LAYER(map, i)->numclasses; j++) {
            /* class styles */
            for(k=0; k<GET_LAYER(map, i)->class[j]->numstyles; k++) {
                if(GET_LAYER(map, i)->class[j]->styles[k]->symbolname) {
                    if((GET_LAYER(map, i)->class[j]->styles[k]->symbol =  msGetSymbolIndex(&(map->symbolset), GET_LAYER(map, i)->class[j]->styles[k]->symbolname, MS_TRUE)) == -1) {
                        msSetError(MS_MISCERR, "Undefined symbol \"%s\" in class %d, style %d of layer %s.", "msLoadMap()", GET_LAYER(map, i)->class[j]->styles[k]->symbolname, j, k, GET_LAYER(map, i)->name);
                        return MS_FAILURE;
                    }
                }
                if(!MS_IS_VALID_ARRAY_INDEX(GET_LAYER(map, i)->class[j]->styles[k]->symbol, map->symbolset.numsymbols)) {
                    msSetError(MS_MISCERR, "Invalid symbol index in class %d, style %d of layer %s.", "msLoadMap()", j, k, GET_LAYER(map, i)->name);
                    return MS_FAILURE;
                }
            }

            /* label styles */
            for(k=0; k<GET_LAYER(map, i)->class[j]->label.numstyles; k++) {
                if(GET_LAYER(map, i)->class[j]->label.styles[k]->symbolname) {
                    if((GET_LAYER(map, i)->class[j]->label.styles[k]->symbol =  msGetSymbolIndex(&(map->symbolset), GET_LAYER(map, i)->class[j]->label.styles[k]->symbolname, MS_TRUE)) == -1) {
                        msSetError(MS_MISCERR, "Undefined symbol \"%s\" in class %d, label style %d of layer %s.", 
                                   "msLoadMap()", GET_LAYER(map, i)->class[j]->label.styles[k]->symbolname, j, k, GET_LAYER(map, i)->name);
                        return MS_FAILURE;
                    }
                }
            }          
        }
    }

    return MS_SUCCESS;
}
