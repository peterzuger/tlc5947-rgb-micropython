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
    return rgb8torgb12(get_rgb8(s));
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


static const uint16_t log_map[256] = {
    0   , 4   , 8   , 12  , 16  , 21  , 25  , 29  , 34  , 38  , 43  , 47  , 52  , 56  , 61  , 66  ,
    70  , 75  , 80  , 85  , 90  , 95  , 100 , 105 , 110 , 115 , 120 , 125 , 130 , 136 , 141 , 147 ,
    152 , 157 , 163 , 169 , 174 , 180 , 186 , 192 , 197 , 203 , 209 , 215 , 222 , 228 , 234 , 240 ,
    246 , 253 , 259 , 266 , 272 , 279 , 285 , 292 , 299 , 306 , 313 , 320 , 327 , 334 , 341 , 348 ,
    356 , 363 , 370 , 378 , 385 , 393 , 401 , 408 , 416 , 424 , 432 , 440 , 448 , 457 , 465 , 473 ,
    482 , 490 , 499 , 507 , 516 , 525 , 534 , 543 , 552 , 561 , 570 , 579 , 589 , 598 , 608 , 618 ,
    627 , 637 , 647 , 657 , 667 , 677 , 688 , 698 , 708 , 719 , 730 , 740 , 751 , 762 , 773 , 784 ,
    796 , 807 , 818 , 830 , 842 , 853 , 865 , 877 , 889 , 902 , 914 , 926 , 939 , 951 , 964 , 977 ,
    990 , 1003, 1016, 1030, 1043, 1057, 1071, 1084, 1098, 1112, 1127, 1141, 1156, 1170, 1185, 1200,
    1215, 1230, 1245, 1261, 1276, 1292, 1308, 1324, 1340, 1356, 1373, 1389, 1406, 1423, 1440, 1457,
    1474, 1492, 1510, 1527, 1545, 1564, 1582, 1600, 1619, 1638, 1657, 1676, 1695, 1715, 1735, 1754,
    1774, 1795, 1815, 1836, 1856, 1877, 1899, 1920, 1941, 1963, 1985, 2007, 2030, 2052, 2075, 2098,
    2121, 2144, 2168, 2192, 2216, 2240, 2265, 2289, 2314, 2339, 2365, 2390, 2416, 2442, 2468, 2495,
    2522, 2549, 2576, 2603, 2631, 2659, 2687, 2716, 2745, 2774, 2803, 2832, 2862, 2892, 2923, 2953,
    2984, 3016, 3047, 3079, 3111, 3143, 3176, 3209, 3242, 3276, 3309, 3344, 3378, 3413, 3448, 3483,
    3519, 3555, 3591, 3628, 3665, 3703, 3740, 3778, 3817, 3855, 3895, 3934, 3974, 4014, 4055, 4095
};

rgb12 rgb8torgb12(rgb8 c){
    rgb12 _c;
    _c.r = log_map[c.r];
    _c.g = log_map[c.g];
    _c.b = log_map[c.b];
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


#endif /* defined(MODULE_TLC5947_ENABLED) && MODULE_TLC5947_ENABLED == 1 */
