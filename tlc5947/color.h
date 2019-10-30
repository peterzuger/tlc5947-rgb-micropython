/**
 * @file   color.h
 * @author Peter Zueger
 * @date   11.09.2018
 * @brief  tlc5947 RGB color handling
 *
 * This file is part of tlc5947-rgb-micropython (https://gitlab.com/peterzuger/tlc5947-rgb-micropython).
 * Copyright (c) 2019 Peter Züger.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TLC5947_COLOR_H
#define TLC5947_COLOR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct{
    uint16_t r:12; /*< red   [0 -> 4095] */
    uint16_t g:12; /*< green [0 -> 4095] */
    uint16_t b:12; /*< blue  [0 -> 4095] */
}rgb12;

typedef struct{
    uint8_t r; /*< red   [0 -> 255] */
    uint8_t g; /*< green [0 -> 255] */
    uint8_t b; /*< blue  [0 -> 255] */
}rgb8;

typedef struct{
    float r; /*< red   [0 -> 1] */
    float g; /*< green [0 -> 1] */
    float b; /*< blue  [0 -> 1] */
}rgb;

typedef struct{
    float h; /*< hue        [0 -> 360] */
    float s; /*< saturation [0 -> 1] */
    float v; /*< value      [0 -> 1] */
}hsv;


#if defined(__cplusplus)
extern "C"{
#endif /* defined(__cplusplus) */


/**
 * takes a string in the form of:
 *     "#RRGGBB"
 * and converts it to the rgb struct
 * does no sanity checking !
 * the # is ignored and can be any character
 * the hex string can be upper or lowercase
 */
rgb12 get_rgb12(const char* s);
rgb8 get_rgb8(const char* s);
rgb  get_rgb (const char* s);


rgb8 rgb12torgb8(rgb12 c)__attribute__ ((const));
rgb rgb12torgb(rgb12 c)__attribute__ ((const));

rgb12 rgb8torgb12(rgb8 c)__attribute__ ((const));
rgb rgb8torgb(rgb8 c)__attribute__ ((const));

rgb12 rgbtorgb12(rgb c)__attribute__ ((const));
rgb8 rgbtorgb8(rgb c)__attribute__ ((const));

hsv hsvfade(hsv a, hsv b, uint32_t steps, uint32_t step);
rgb12 rgb12fade(rgb12 a, rgb12 b, uint32_t steps, uint32_t step);

#define rgb12valid (1) /* cannot be invalid */
#define rgb8valid (1) /* cannot be invalid */
bool rgbvalid(rgb c)__attribute__ ((const));
bool hsvvalid(hsv c)__attribute__ ((const));


rgb hsvtorgb(hsv c)__attribute__ ((const));
hsv rgbtohsv(rgb c)__attribute__ ((const));


#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* TLC5947_COLOR_H */
