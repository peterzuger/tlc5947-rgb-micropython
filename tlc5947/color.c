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
#include <string.h>
#include "color.h"

#define RGB12_MAGIC1 (16.0588235F) /* ((2^12)-1) / ((2^8)-1) */
#define RGB12_MAGIC2 (16.0588236F) /*  */

/**
 * these functions should be provided by libc <ctype.h>
 * but :
 * color.c:(.text.get_byte+0x6): undefined reference to `__locale_ctype_ptr'
 * copied from:
 *     https://gitlab.com/peterzuger/libc/tree/master/src/ctype
 */
static int islower(int c){return ((c>='a')&&(c<='z'));}
static int toupper(int c){return islower(c)?(c-32):c;}
static char get_hex(uint8_t i){return "0123456789ABCDEF"[i];}

static uint8_t get_byte(const char* s){
    uint8_t i;
    i  = ((toupper(*s)) < 58 ? (*s++) - 48 : (*s++) - 55) << 4;
    i |= ((toupper(*s)) < 58 ? (*s  ) - 48 : (*s  ) - 55);
    return i;
}

static void put_byte(char* s, uint8_t b){
    s[0] = get_hex((b & 0xF0) >> 4);
    s[1] = get_hex((b & 0x0F)     );
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

const char* put_rgb8(char* s, rgb8 c){
    s[0] = '#';
    put_byte(s + 1, c.r);
    put_byte(s + 3, c.g);
    put_byte(s + 5, c.b);
    s[7] = 0;
    return s + 7;
}

rgb8 rgb12torgb8(rgb12 c){
    rgb8 _c;
    _c.r = (uint8_t)(c.r / RGB12_MAGIC1);
    _c.g = (uint8_t)(c.g / RGB12_MAGIC1);
    _c.b = (uint8_t)(c.b / RGB12_MAGIC1);
    return _c;
}


rgb12 rgb8torgb12(rgb8 c){
    rgb12 _c;
    _c.r = (uint16_t)(c.r * RGB12_MAGIC2);
    _c.g = (uint16_t)(c.g * RGB12_MAGIC2);
    _c.b = (uint16_t)(c.b * RGB12_MAGIC2);
    return _c;
}

static const uint16_t logLUT[2][12] = { {
        0,  353, 1109, 1990, 2614, 3495, 4120, 5000, 5775, 6990, 8495, 10000},
      { 0, 1500, 4000, 6000, 7000, 8000, 8500, 9000, 9300, 9600, 9800, 10000 }
};

static float log_brightness(float f_linval){
    uint8_t i;
    uint16_t ipf;
    uint16_t linval = (uint16_t)(f_linval * ((float)10000));

    if(linval >= logLUT[1][11]){
        ipf = logLUT[0][11];
    }else{
        for(i = 0; i < 11; i++){
            if(linval < logLUT[1][i]){
                break;
            }
        }

        ipf = (linval - logLUT[1][i - 1]) * 10 / (logLUT[1][i] - logLUT[1][i - 1]);
        ipf = logLUT[0][i - 1] + (logLUT[0][i] - logLUT[0][i - 1]) * ipf / 10;
    }

    return (float)(((float)ipf) / ((float)10000));
}


rgb12 rgb12_brightness(rgb12 c, float brightness){
    rgb12 _c;

    float b = log_brightness(brightness);

    _c.r = (uint16_t)((float)c.r * b);
    _c.g = (uint16_t)((float)c.g * b);
    _c.b = (uint16_t)((float)c.b * b);

    return _c;
}

void default_white_balance(white_balance_matrix m){
    for(uint8_t i = 0; i < 3; i++)
        m[i] = 1.0F;
}

rgb12 rgb12_white_balance(rgb12 c, white_balance_matrix m){
    rgb12 _c;
    _c.r = (uint16_t)(c.r * m[0]);
    _c.g = (uint16_t)(c.g * m[1]);
    _c.b = (uint16_t)(c.b * m[2]);
    return _c;
}


#define ADD_F(a, b) ((float)(((float)a)+((float)b)))
#define ADD3_F(a, b, c) ((float)(ADD_F(ADD_F(a, b), ((float)(c)))))
bool gamut_matrix_valid(gamut_matrix m){
    for(uint8_t i = 0; i < 3; ++i)
        if( ADD3_F(m[i][0], m[i][1], m[i][2]) > 1.0F )
            return false;
    return true;
}

void default_gamut_matrix(gamut_matrix m){
    memset(m, 0, sizeof(gamut_matrix));
    for(uint8_t i = 0; i < 3; i++)
        m[i][i] = 1.0F;
}

rgb12 rgb12_gamut(rgb12 c, gamut_matrix m){
    rgb12 _c;
    _c.r = (uint16_t)((c.r * m[0][0]) + (c.g * m[0][1]) + (c.b * m[0][2]));
    _c.g = (uint16_t)((c.r * m[1][0]) + (c.g * m[1][1]) + (c.b * m[1][2]));
    _c.b = (uint16_t)((c.r * m[2][0]) + (c.g * m[2][1]) + (c.b * m[2][2]));
    return _c;
}


#endif /* defined(MODULE_TLC5947_ENABLED) && MODULE_TLC5947_ENABLED == 1 */
