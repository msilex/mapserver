# $Id$
#
# Project:  MapServer
# Purpose:  xUnit style Python mapscript tests of Map
# Author:   Umberto Nicoletti, unicoletti@prometeo.it
#
# ===========================================================================
# Copyright (c) 2004, Sean Gillies
# 
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
# ===========================================================================
#
# Execute this module as a script from mapserver/mapscript/python
#
#     python tests/cases/refcount.py -v
#
# ===========================================================================

import os, sys
import unittest

# the testing module helps us import the pre-installed mapscript
from testing import mapscript, MapTestCase, TESTMAPFILE

# ===========================================================================
# Test begins now

class ReferenceCountingTestCase(unittest.TestCase):

    def initMap(self):
	self.map=mapscript.mapObj(TESTMAPFILE)

    def testMap(self):
        """ReferenceCountingTestCase.testMap: test map constructor with no argument"""
        test_map = mapscript.mapObj()
        maptype = type(test_map)
        assert str(maptype) == "<class 'mapscript.mapObj'>", maptype
        assert test_map.thisown == 1
	assert test_map.refcount == 1
    
    def testMapWithDefaultMap(self):
        """ReferenceCountingTestCase.testTestMap: test map constructor with default map file"""
        test_map = mapscript.mapObj(TESTMAPFILE)
        maptype = type(test_map)
        assert str(maptype) == "<class 'mapscript.mapObj'>", maptype
        assert test_map.thisown == 1
	assert test_map.refcount == 1
	assert test_map.getLayer(0).refcount == 2
   
    def testMapLayersCounting(self):
        """ReferenceCountingTestCase.testTestMap: test map constructor with default map file"""
        self.initMap()
	layerRef1=self.map.getLayer(0)
	assert layerRef1.refcount == 2
	
	layerRef2=self.map.getLayer(0)
	assert layerRef2.refcount == 3

    def testMapInsertedLayer(self):
        """MapLayersTestCase.testMapInsertedLayer: test insertion of a new layer at default (last) index"""
	self.initMap()
        n = self.map.numlayers
        layer = mapscript.layerObj()
        layer.name = 'new'
	assert layer.refcount == 1
	assert layer.thisown == 1
        index = self.map.insertLayer(layer)
        assert index == n, index
        assert self.map.numlayers == n + 1
	assert layer.refcount == 2
	assert layer.thisown == 1
        assert self.map.getLayer(index).refcount == 3
	assert self.map.getLayer(index).thisown == 1
	assert layer.refcount == 2 
	assert layer.thisown == 1

    def testMapInsertedLayerWithIndex(self):
        """MapLayersTestCase.testMapInsertedLayerWithIndex: test insertion of a new layer at index 0"""
	self.initMap()
        n = self.map.numlayers
        layer = mapscript.layerObj()
        layer.name = 'new'
	assert layer.refcount == 1
	assert layer.thisown == 1
        index = self.map.insertLayer(layer,0)
        assert index == 0, index
        assert self.map.numlayers == n + 1
	assert layer.refcount == 2
	assert layer.thisown == 1
        assert self.map.getLayer(index).refcount == 3
	assert self.map.getLayer(index).thisown == 1
	assert layer.refcount == 2 
	assert layer.thisown == 1

        
    def testMapRemovedLayerAtTail(self):
        """removal of highest index (tail) layer"""
	self.initMap()
        n = self.map.numlayers
        layer = self.map.removeLayer(n-1)
        assert self.map.numlayers == n-1
        assert layer.name == 'INLINE-PIXMAP-PCT'
        assert layer.thisown == 1
	assert layer.refcount == 1, layer.refcount
	self.map.draw()

    def testMapRemovedLayerAtZero(self):
        """removal of lowest index (0) layer"""
	self.initMap()
        n = self.map.numlayers
        layer = self.map.removeLayer(0)
        assert self.map.numlayers == n-1
        assert layer.name == 'RASTER'
        assert layer.thisown == 1
	assert layer.refcount == 1, layer.refcount
	self.map.draw()
        
    def testBehaveWhenParentIsNull(self):
        """behave when parent (map) is null"""
	self.initMap()
        layer = mapscript.layerObj()
        layer.name = 'new'
        index = self.map.insertLayer(layer,0)
        assert index == 0, index
	self.map=None
	assert layer.map == None, layer.map
	exception=None
	try:
		layer.open()
	except:
		assert True
		exception=True
	if not exception:
		fail

    def testLayerClone(self):
        """Clone a layer"""
        layer = mapscript.layerObj()
	layer.name='sample'
	copy = layer.clone()

	assert layer.refcount == 1, layer.refcount
	assert copy.refcount == 1, copy.refcount
	assert layer.name == copy.name
	assert copy.map == None
    
    def testBehaveWhenParentIsNotNull(self):
        """behave when parent (map) is not null"""
	self.initMap()
        layer = mapscript.layerObj(self.map)
        layer.name = 'new'
	assert layer.refcount == 2, layer.refcount

    def testDummyClass(self):
        """basic refcounting for classObj"""
	clazz = mapscript.classObj()
	assert clazz.refcount == 1, clazz.refcount

    def testClassWithMapArgument(self):
        """classObj constructor with not null layer"""
	self.initMap()
	clazz = mapscript.classObj(self.map.getLayer(0))
	assert clazz.refcount == 2, clazz.refcount

    def testClassGetter(self):
        """classObj getter"""
	self.initMap()
	clazz = self.map.getLayer(1).getClass(0)
	assert clazz.refcount == 2, clazz.refcount
    
    def testClassClone(self):
        """classObj clone"""
	self.initMap()
	clazz = self.map.getLayer(1).getClass(0)
	assert clazz.refcount == 2, clazz.refcount
	clone = clazz.clone()
	assert clone.refcount == 1, clone.refcount

    def testClassInsert(self):
        """classObj insert at end"""
	self.initMap()
	clazz = mapscript.classObj()
	clazz.minscale = 666
	assert clazz.refcount == 1, clazz.refcount
	idx=self.map.getLayer(1).insertClass(clazz)
	assert clazz.refcount == 2, clazz.refcount
	assert self.map.getLayer(1).getClass(idx).refcount == 3, self.map.getLayer(1).getClass(idx).refcount
	assert self.map.getLayer(1).getClass(idx).minscale == 666, self.map.getLayer(1).getClass(idx).minscale

    def testClassInsertAtMiddle(self):
        """classObj insert at pos. 1"""
	self.initMap()
	clazz = mapscript.classObj()
	clazz.minscale = 666
	assert clazz.refcount == 1, clazz.refcount
	idx=self.map.getLayer(1).insertClass(clazz, 1)
	assert idx == 1, idx
	assert clazz.refcount == 2, clazz.refcount
	assert self.map.getLayer(1).getClass(idx).refcount == 3, self.map.getLayer(1).getClass(idx).refcount
	assert self.map.getLayer(1).getClass(idx).thisown , self.map.getLayer(1).getClass(idx).thisown
	assert self.map.getLayer(1).getClass(idx).minscale == 666, self.map.getLayer(1).getClass(idx).minscale
    
    def testMapClone(self):
        """cloning a mapObj"""
	self.initMap()
	clone = self.map.clone()
	assert clone.refcount == 1, clone.refcount

# ===========================================================================
# Run the tests outside of the main suite

if __name__ == '__main__':
    unittest.main()
    
