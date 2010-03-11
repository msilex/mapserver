/**********************************************************************
 * $Id: php_mapscript.c 9765 2010-01-28 15:32:10Z aboudreault $
 *
 * Project:  MapServer
 * Purpose:  PHP/MapScript extension for MapServer.  External interface 
 *           functions
 * Author:   Daniel Morissette, DM Solutions Group (dmorissette@dmsolutions.ca)
 *           Alan Boudreault, Mapgears
 *
 **********************************************************************
 * Copyright (c) 2000-2010, Daniel Morissette, DM Solutions Group Inc.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 **********************************************************************/

#include "php_mapscript.h"

zend_class_entry *mapscript_ce_querymap;

ZEND_BEGIN_ARG_INFO_EX(querymap___get_args, 0, 0, 1)
  ZEND_ARG_INFO(0, property)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(querymap___set_args, 0, 0, 2)
  ZEND_ARG_INFO(0, property)
  ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(querymap_updateFromString_args, 0, 0, 1)
  ZEND_ARG_INFO(0, snippet)
ZEND_END_ARG_INFO()

/* {{{ proto querymap __construct()
   queryMapObj CANNOT be instanciated, this will throw an exception on use */
PHP_METHOD(queryMapObj, __construct)
{
    mapscript_throw_exception("queryMapObj cannot be constructed");
}
/* }}} */

PHP_METHOD(queryMapObj, __get)
{
    char *property;
    int property_len;
    zval *zobj = getThis();
    php_querymap_object *php_querymap;

    PHP_MAPSCRIPT_ERROR_HANDLING(TRUE);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
                              &property, &property_len) == FAILURE) {
        PHP_MAPSCRIPT_RESTORE_ERRORS(TRUE);
        return;
    }
    PHP_MAPSCRIPT_RESTORE_ERRORS(TRUE);
    
    php_querymap = (php_querymap_object *) zend_object_store_get_object(zobj TSRMLS_CC);

    IF_GET_LONG("width", php_querymap->querymap->width)
    else IF_GET_LONG("height", php_querymap->querymap->height)
    else IF_GET_LONG("style", php_querymap->querymap->style)
    else IF_GET_OBJECT("color", php_querymap->color)
    else 
    {
        mapscript_throw_exception("Property '%s' does not exist in this object.", property);
    }
}

PHP_METHOD(queryMapObj, __set)
{
    char *property;
    int property_len;
    zval *value;
    zval *zobj = getThis();
    php_querymap_object *php_querymap;

    PHP_MAPSCRIPT_ERROR_HANDLING(TRUE);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz",
                              &property, &property_len, &value) == FAILURE) {
        PHP_MAPSCRIPT_RESTORE_ERRORS(TRUE);
        return;
    }
    PHP_MAPSCRIPT_RESTORE_ERRORS(TRUE);
    
    php_querymap = (php_querymap_object *) zend_object_store_get_object(zobj TSRMLS_CC);

    IF_SET_LONG("width", php_querymap->querymap->width, value)
    else IF_SET_LONG("height", php_querymap->querymap->height, value)
    else IF_SET_LONG("style", php_querymap->querymap->style, value)
    else if ( (STRING_EQUAL("color", property)))
    {
        mapscript_throw_exception("Property '%s' is an object and can only be modified through its accessors.", property);
    }
    else 
    {
        mapscript_throw_exception("Property '%s' does not exist in this object.", property);
    }
}

/* {{{ proto int querymap.updateFromString(string snippet)
   Update a querymap from a string snippet.  Returns MS_SUCCESS/MS_FAILURE */
PHP_METHOD(queryMapObj, updateFromString)
{
    char *snippet;
    int snippet_len;
    zval *zobj = getThis();
    php_querymap_object *php_querymap;
    int status = MS_FAILURE;

    PHP_MAPSCRIPT_ERROR_HANDLING(TRUE);
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
                              &snippet, &snippet_len) == FAILURE) {
        PHP_MAPSCRIPT_RESTORE_ERRORS(TRUE);
        return;
    }
    PHP_MAPSCRIPT_RESTORE_ERRORS(TRUE);
    
    php_querymap = (php_querymap_object *) zend_object_store_get_object(zobj TSRMLS_CC);

    status =  queryMapObj_updateFromString(php_querymap->querymap, snippet);
    
    if (status != MS_SUCCESS)
    {
        mapscript_throw_mapserver_exception("");
        return;
    }

    RETURN_LONG(status);
}
/* }}} */

zend_function_entry querymap_functions[] = {
    PHP_ME(queryMapObj, __construct, NULL, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
    PHP_ME(queryMapObj, __get, querymap___get_args, ZEND_ACC_PUBLIC)
    PHP_ME(queryMapObj, __set, querymap___set_args, ZEND_ACC_PUBLIC)
    PHP_MALIAS(queryMapObj, set, __set, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(queryMapObj, updateFromString, querymap_updateFromString_args, ZEND_ACC_PUBLIC)
    {NULL, NULL, NULL}
};

void mapscript_create_querymap(queryMapObj *querymap, zval *php_parent, zval *return_value TSRMLS_DC)
{
    php_querymap_object * php_querymap;
    object_init_ex(return_value, mapscript_ce_querymap); 
    php_querymap = (php_querymap_object *)zend_object_store_get_object(return_value TSRMLS_CC);
    php_querymap->querymap = querymap;

    MAKE_STD_ZVAL(php_querymap->color);
    mapscript_create_color(&(querymap->color), return_value, php_querymap->color TSRMLS_CC);

    php_querymap->parent = php_parent;
    MAPSCRIPT_ADDREF(php_parent);

}

static void mapscript_querymap_object_destroy(void *object TSRMLS_DC)
{
    php_querymap_object *php_querymap = (php_querymap_object *)object;

    MAPSCRIPT_FREE_OBJECT(php_querymap);

    MAPSCRIPT_DELREF(php_querymap->parent);
    MAPSCRIPT_DELREF(php_querymap->color);

    /* We don't need to free the queryMapObj */ 
    
    efree(object);
}

static zend_object_value mapscript_querymap_object_new(zend_class_entry *ce TSRMLS_DC)
{
    zend_object_value retval;
    php_querymap_object *php_querymap;

    MAPSCRIPT_ALLOC_OBJECT(php_querymap, php_querymap_object);

    retval = mapscript_object_new(&php_querymap->std, ce,
                                  &mapscript_querymap_object_destroy);

    php_querymap->parent = NULL;
    php_querymap->color = NULL;

    return retval;
}

PHP_MINIT_FUNCTION(querymap)
{
    zend_class_entry ce;

    MAPSCRIPT_REGISTER_CLASS("queryMapObj", 
                             querymap_functions,
                             mapscript_ce_querymap,
                             mapscript_querymap_object_new);

    mapscript_ce_querymap->ce_flags |= ZEND_ACC_FINAL_CLASS; 
    
    return SUCCESS;
}
