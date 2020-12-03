/**
 * @file   tlc5947-rgb-micropython/tlc5947/color.c
 * @author Peter Züger
 * @date   11.09.2018
 * @brief  tlc5947 RGB color handling
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Peter Züger
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "py/mpconfig.h"

#if defined(MODULE_TLC5947_ENABLED) && MODULE_TLC5947_ENABLED == 1

#include <math.h>
#include "color.h"

#define RGB12_MAGIC1 (16.0588235) /* ((2^12)-1) / ((2^8)-1) */
#define RGB12_MAGIC2 (16.0588236) /*  */

/**
 * these functions should be provided by libc <ctype.h>
 * but :
 * color.c:(.text.get_byte+0x6): undefined reference to `__locale_ctype_ptr'
 * copied from:
 *     https://gitlab.com/peterzuger/libc/tree/master/src/ctype
 */
static int islower(int c){return ((c>='a')&&(c<='z'));}
static int toupper(int c){return islower(c)?(c-32):c;}

static uint8_t get_byte(const char* s){
    uint8_t i;
    i  = ((toupper(*s)) < 58 ? (*s++) - 48 : (*s++) - 55) << 4;
    i |= ((toupper(*s)) < 58 ? (*s  ) - 48 : (*s  ) - 55);
    return i;
}


rgb12 get_rgb12(const char* s){
    rgb12 c;
    c.r = (uint16_t)(get_byte(&s[1]) * RGB12_MAGIC2);
    c.g = (uint16_t)(get_byte(&s[3]) * RGB12_MAGIC2);
    c.b = (uint16_t)(get_byte(&s[5]) * RGB12_MAGIC2);
    return c;
}

rgb8 get_rgb8(const char* s){
    rgb8 c;
    c.r = get_byte(&s[1]);
    c.g = get_byte(&s[3]);
    c.b = get_byte(&s[5]);
    return c;
}

rgb get_rgb(const char* s){
    rgb8 c = get_rgb8(s);
    rgb _c;
    _c.r = (float)c.r / 255;
    _c.g = (float)c.g / 255;
    _c.b = (float)c.b / 255;
    return _c;
}


rgb8 rgb12torgb8(rgb12 c){
    rgb8 _c;
    _c.r = (uint8_t)(c.r / RGB12_MAGIC1);
    _c.g = (uint8_t)(c.g / RGB12_MAGIC1);
    _c.b = (uint8_t)(c.b / RGB12_MAGIC1);
    return _c;
}

rgb rgb12torgb(rgb12 c){
    rgb _c;
    _c.r = ((float)c.r) / ((float)4095);
    _c.g = ((float)c.g) / ((float)4095);
    _c.b = ((float)c.b) / ((float)4095);
    return _c;
}


rgb12 rgb8torgb12(rgb8 c){
    rgb12 _c;
    _c.r = (uint16_t)(c.r * RGB12_MAGIC1);
    _c.g = (uint16_t)(c.g * RGB12_MAGIC1);
    _c.b = (uint16_t)(c.b * RGB12_MAGIC1);
    return _c;
}

rgb rgb8torgb(rgb8 c){
    rgb _c;
    _c.r = ((float)c.r) / ((float)255);
    _c.g = ((float)c.g) / ((float)255);
    _c.b = ((float)c.b) / ((float)255);
    return _c;
}


rgb8 rgbtorgb8(rgb c){
    rgb8 _c;
    _c.r = (uint8_t)(c.r * ((float)255));
    _c.g = (uint8_t)(c.g * ((float)255));
    _c.b = (uint8_t)(c.b * ((float)255));
    return _c;
}

rgb12 rgbtorgb12(rgb c){
    rgb12 _c;
    _c.r = (uint16_t)(c.r * ((float)4095));
    _c.g = (uint16_t)(c.g * ((float)4095));
    _c.b = (uint16_t)(c.b * ((float)4095));
    return _c;
}

hsv hsvfade(hsv a, hsv b, uint32_t steps, uint32_t step){
    hsv c;
    c.h = a.h + (((b.h - a.h) / steps) * step);
    c.s = a.s + (((b.s - a.s) / steps) * step);
    c.v = a.v + (((b.v - a.v) / steps) * step);
    return c;
}

rgb12 rgb12fade(rgb12 a, rgb12 b, uint32_t steps, uint32_t step){
    rgb12 c;
    c.r = (a.r > b.r) ? a.r - (((a.r - b.r) / steps) * step) : a.r + (((b.r - a.r) / steps) * step);
    c.g = (a.g > b.g) ? a.g - (((a.g - b.g) / steps) * step) : a.g + (((b.g - a.g) / steps) * step);
    c.b = (a.b > b.b) ? a.b - (((a.b - b.b) / steps) * step) : a.b + (((b.b - a.b) / steps) * step);
    return c;
}

bool rgbvalid(rgb c){
    if((c.r > 1) || (c.g > 1) || (c.b > 1) ||
       ((c.r < 0) || (c.g < 0) || (c.b < 0)))
        return false;
    return true;
}

bool hsvvalid(hsv c){
    if((c.s > 1) || (c.v > 1) || (c.h > 360) ||
       ((c.s < 0) || (c.v < 0) || (c.h < 0)))
        return false;
    return true;
}


/**
 * typesafe min(a,b) / max(a,b) macros
 * should probably be moved to a universal macro header...
 */
#define max(a,b)                                \
    ({ __typeof__ (a) _a = (a);                 \
        __typeof__ (b) _b = (b);                \
        _a > _b ? _a : _b; })
#define min(a,b)                                \
    ({ __typeof__ (a) _a = (a);                 \
        __typeof__ (b) _b = (b);                \
        _a < _b ? _a : _b; })

#define max3(a,b,c) (max(max(a,b),c))
#define min3(a,b,c) (min(min(a,b),c))

hsv rgbtohsv(rgb c){
    hsv _c;
    float min, max, delta;
    min = min3(c.r, c.g, c.b);
    max = max3(c.r, c.g, c.b);
    _c.v = max;                         // v
    delta = max - min;
    if(max != 0){
        _c.s = delta / max;             // s
    }else{
        // r = g = b = 0                // s = 0, v is undefined
        _c.s = 0;
        _c.h = -1;
        return _c;
    }

    if(c.r == max)
        _c.h = ( c.g - c.b ) / delta;        // between yellow & magenta
    else if( c.g == max )
        _c.h = 2 + ( c.b - c.r ) / delta;    // between cyan & yellow
    else
        _c.h = 4 + ( c.r - c.g ) / delta;    // between magenta & cyan
    _c.h *= 60;                              // degrees
    if(_c.h < 0)
        _c.h += 360;
    return _c;
}

rgb hsvtorgb(hsv c){
    rgb _c;
    int i;
    float f, p, q, t;
    if(c.s == 0 || c.v == 0){
        // achromatic (grey)
        _c.r = _c.g = _c.b = c.v;
        return _c;
    }
    c.h /= 60;                          // sector 0 to 5
    i = (int)floorf( c.h );
    f = c.h - i;                        // factorial part of h
    p = c.v * ( 1 - c.s );
    q = c.v * ( 1 - c.s * f );
    t = c.v * ( 1 - c.s * ( 1 - f ) );
    switch(i){
    case 0:
        _c.r = c.v;
        _c.g = t;
        _c.b = p;
        break;
    case 1:
        _c.r = q;
        _c.g = c.v;
        _c.b = p;
        break;
    case 2:
        _c.r = p;
        _c.g = c.v;
        _c.b = t;
        break;
    case 3:
        _c.r = p;
        _c.g = q;
        _c.b = c.v;
        break;
    case 4:
        _c.r = t;
        _c.g = p;
        _c.b = c.v;
        break;
    default:
        _c.r = c.v;
        _c.g = p;
        _c.b = q;
        break;
    }
    return _c;
}

#endif /* defined(MODULE_TLC5947_ENABLED) && MODULE_TLC5947_ENABLED == 1 */
