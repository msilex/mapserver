# $Id$
#
# Project:  MapServer
# Purpose:  xUnit style Python mapscript tests of Map "zooming"
# Author:   Sean Gillies, sgillies@frii.com
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
#     python tests/cases/styletest.py -v
#
# ===========================================================================

import os, sys
import unittest

# the testing module helps us import the pre-installed mapscript
from testing import mapscript, MapTestCase

class DrawProgrammedStylesTestCase(MapTestCase):
    def testDrawPoints(self):
        """DrawProgrammedStylesTestCase.testDrawPoints: point drawing with styles works as advertised"""
        points = [mapscript.pointObj(-0.2, 51.6),
                  mapscript.pointObj(0.0, 51.2),
                  mapscript.pointObj(0.2, 51.6)]
        colors = [mapscript.colorObj(255,0,0),
                  mapscript.colorObj(0,255,0),
                  mapscript.colorObj(0,0,255)]
        img = self.map.prepareImage()
        layer = self.map.getLayerByName('POINT')
        #layer.draw(self.map, img)
        class0 = layer.getClass(0)
        for i in range(len(points)):
            style0 = class0.getStyle(0)
            style0.color = colors[i]
            #style0.color.pen = -4
            assert style0.color.toHex() == colors[i].toHex()
            points[i].draw(self.map, layer, img, 0, "foo")
        img.save('test_draw_points.png')

class NewStylesTestCase(MapTestCase):
    def testStyleConstructor(self):
        """NewStylesTestCase.testStyleConstructor: a new style is properly initialized"""
        new_style = mapscript.styleObj()
        assert new_style.color.red == -1
        assert new_style.color.green == -1
        assert new_style.color.blue == -1
    def testStyleColorSettable(self):
        """NewStylesTestCase.testStyleColorSettable: a style can be set with a color tuple"""
        new_style = mapscript.styleObj()
        new_style.color.setRGB(1,2,3)
        assert new_style.color.red == 1
        assert new_style.color.green == 2
        assert new_style.color.blue == 3
    def testAppendNewStyle(self):
        """NewStylesTestCase.testAppendNewStyle: a new style can be appended properly"""
        p_layer = self.map.getLayerByName('POINT')
        class0 = p_layer.getClass(0)
        assert class0.numstyles == 2, class0.numstyles
        new_style = mapscript.styleObj()
        new_style.color.setRGB(0, 0, 0)
        new_style.symbol = 1
        new_style.size = 3
        index = class0.insertStyle(new_style)
        assert index == 2, index
        assert class0.numstyles == 3, class0.numstyles
        msimg = self.map.draw()
        assert msimg.thisown == 1
        data = msimg.saveToString()
        filename = 'testAppendNewStyle.png'
        fh = open(filename, 'wb')
        fh.write(data)
        fh.close()
    def testAppendNewStyleOldWay(self):
        """NewStylesTestCase.testAppendNewStyleOldWay: a new style can be appended properly using old method"""
        p_layer = self.map.getLayerByName('POINT')
        class0 = p_layer.getClass(0)
        assert class0.numstyles == 2, class0.numstyles
        new_style = mapscript.styleObj(class0)
        assert new_style.thisown == 1, new_style.thisown
        new_style.color.setRGB(0, 0, 0)
        new_style.symbol = 1
        new_style.size = 3
        msimg = self.map.draw()
        data = msimg.saveToString()
        filename = 'testAppendNewStyleOldWay.png'
        fh = open(filename, 'wb')
        fh.write(data)
        fh.close()
    def testInsertNewStyleAtIndex0(self):
        """NewStylesTestCase.testInsertNewStyleAtIndex0: a new style can be inserted ahead of all others"""
        l_layer = self.map.getLayerByName('LINE')
        class0 = l_layer.getClass(0)
        new_style = mapscript.styleObj()
        new_style.color.setRGB(255, 255, 0)
        new_style.symbol = 1
        new_style.size = 7
        index = class0.insertStyle(new_style, 0)
        assert index == 0, index
        assert class0.numstyles == 2, class0.numstyles
        msimg = self.map.draw()
        assert msimg.thisown == 1
        data = msimg.saveToString()
        filename = 'testInsertNewStyleAtIndex0.png'
        fh = open(filename, 'wb')
        fh.write(data)
        fh.close()
    def testRemovePointStyle(self):
        """NewStylesTestCase.testRemovePointStyle: a point style can be properly removed"""
        p_layer = self.map.getLayerByName('POINT')
        class0 = p_layer.getClass(0)
        rem_style = class0.removeStyle(1)
        assert class0.numstyles == 1, class0.numstyles
        msimg = self.map.draw()
        filename = 'testRemovePointStyle.png'
        msimg.save(filename)
    def testModifyMultipleStyle(self):
        """NewStylesTestCase.testModifyMultipleStyle: multiple styles can be modified"""
        p_layer = self.map.getLayerByName('POINT')
        class0 = p_layer.getClass(0)
        style1 = class0.getStyle(1)
        style1.color.setRGB(255, 255, 0)
        msimg = self.map.draw()
        filename = 'testModifyMutiplePointStyle.png'
        msimg.save(filename)
    def testInsertTooManyStyles(self):
        """NewStylesTestCase.testInsertTooManyStyles: inserting too many styles raises the proper error"""
        p_layer = self.map.getLayerByName('POINT')
        class0 = p_layer.getClass(0)
        new_style = mapscript.styleObj()
        # Add three styles successfully
        index = class0.insertStyle(new_style)
        index = class0.insertStyle(new_style)
        index = class0.insertStyle(new_style)
        # We've reached the maximum, next attempt should raise exception
        self.assertRaises(mapscript.MapServerChildError, class0.insertStyle, new_style)
    def testInsertStylePastEnd(self):
        """NewStylesTestCase.testInsertStylePastEnd: inserting a style past the end of the list raises the proper error"""
        p_layer = self.map.getLayerByName('POINT')
        class0 = p_layer.getClass(0)
        new_style = mapscript.styleObj()
        self.assertRaises(mapscript.MapServerChildError, class0.insertStyle, new_style, 6)

class ColorObjTestCase(unittest.TestCase):
    def testColorObjConstructorNoArgs(self):
        """ColorObjTestCase.testColorObjConstructorNoArgs: a color can be initialized with no arguments"""
        c = mapscript.colorObj()
        assert (c.red, c.green, c.blue, c.pen) == (0, 0, 0, -4)
    def testColorObjConstructorArgs(self):
        """ColorObjTestCase.testColorObjConstructorArgs: a color can be initialized with arguments"""
        c = mapscript.colorObj(1, 2, 3)
        assert (c.red, c.green, c.blue, c.pen) == (1, 2, 3, -4)
    def testColorObjToHex(self):
        """ColorObjTestCase.testColorObjToHex: a color can be outputted as hex"""
        c = mapscript.colorObj(255, 255, 255)
        assert c.toHex() == '#ffffff'
    def testColorObjSetRGB(self):
        """ColorObjTestCase.testColorObjSetRGB: a color can be set using setRGB method"""
        c = mapscript.colorObj()
        c.setRGB(255, 255, 255)
        assert (c.red, c.green, c.blue, c.pen) == (255, 255, 255, -4)
    def testColorObjSetHexLower(self):
        """ColorObjTestCase.testColorObjSetHexLower: a color can be set using lower case hex"""
        c = mapscript.colorObj()
        c.setHex('#ffffff')
        assert (c.red, c.green, c.blue, c.pen) == (255, 255, 255, -4)
    def testColorObjSetHexUpper(self):
        """ColorObjTestCase.testColorObjSetHexUpper: a color can be set using upper case hex"""
        c = mapscript.colorObj()
        c.setHex('#FFFFFF')
        assert (c.red, c.green, c.blue) == (255, 255, 255)
    def testColorObjSetHexBadly(self):
        """ColorObjTestCase.testColorObjSetHexBadly: invalid hex color string raises proper error"""
        c = mapscript.colorObj()
        self.assertRaises(mapscript.MapServerError, c.setHex, '#fffffg')
        
if __name__ == '__main__':
    unittest.main()
