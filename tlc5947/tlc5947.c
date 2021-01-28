/**
 * @file   tlc5947-rgb-micropython/tlc5947/tlc5947.c
 * @author Peter Züger
 * @date   30.08.2018
 * @brief  tlc5947 RGB led driver
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

#include <string.h>
#include <stdio.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "pin.h"
#include "spi.h"

#include "color.h"

/**
 * LED language
 *
 * "#RRGGBB"      this is a color in RGB format
 * "$220,1.0,0.5" this is a color in HSV format
 * "|50"          this sleeps for 50 ticks
 * "\b25"         this decreases brightness by 25%
 * "<5"           this pushes 5 onto the stack
 * ">"            this pops a value from the stack
 * "+"            increment current stack value
 * "-"            decrement current stack value
 * "]"            Jump to the matching marker if stack value is not 0
 * "["            Marker
 * ";"            loop forever
 * "@"            toggle the transparency
 *
 * examples:
 *   "+[#FFFFFF|500#000000|500]"
 *     This is an infinite loop, that changes the led from "white"(#FFFFFF)
 *     to "black"(#000000) every 500 ticks.
 *     This works by first incrementing the initial stack value from 0 to 1,
 *     then it sets up a marker for the jne to jump to then it sets the led
 *     to white, sleeps for 500 ticks and then sets the color to black and
 *     sleeps for 500 ticks again. Because the stack value is not 0 it then
 *     jumps to the Marker and the cycle repeats
 *
 *   "#0000FF"
 *     This sets the led to blue, this can be used to set a "background color",
 *     if this is the first pattern set on this led, the led will be blue
 *     if all other patterns are completed
 *
 *   "<5[#FF0000<10[|50\b-0.1-]>-|50]"
 *     This is a more complex pattern, it sets the led to full red, and then
 *     every 50 ticks decreases the brightness by 0.1 in HSV color space and
 *     this is repeated 5 times.
 */
typedef enum{
    pCOLOR,       // change color
    pTRANSPARENT, // toggle the transparency
    pSLEEP,       // sleep for x amount of ticks
    pBRIGHTNESS,  // change overall brightness
    pINCREMENT,   // increment current stack value
    pDECREMENT,   // decrement current stack value
    pFOREVER,     // dont do anything
    pJUMP_NZERO,  // jump to the matching marker if stack value is not 0
    pMARK,        // Marker for jump
    pPUSH,        // Push value onto the stack
    pPOP          // Pop value from the stack
}token_type_t;

/**
 * This data structure contains all the tokens of the led language.
 * The first element of this type is the enum from above,
 * this is to identify wich token this instance refers to
 *
 * Then follows a single union, containing an element for every
 * possible value of the enum, even if they are empty.
 *
 * This union member corresponding to the respective enum constant,
 * holds the data required for the specific syntax token.
 *
 * This structure is not allowed to contain any allocated memory.
 */
typedef struct _token_t{
    token_type_t type;
    union{
        struct{rgb12 color;                            }color;
        struct{                                        }transparent;
        struct{uint32_t sleep_time; uint32_t remaining;}sleep;
        struct{float brightness;                       }brighness;
        struct{                                        }increment;
        struct{                                        }decrement;
        struct{                                        }forever;
        struct{uint16_t new_pp;                        }jump;
        struct{                                        }mark;
        struct{int8_t value;                           }push;
        struct{                                        }pop;
    };
}token_t;


#define MAX_STACK 10
typedef struct _pattern_base_t{
    uint16_t id;         // this is the pattern id that maps to th pattern map
    uint16_t len;
    uint16_t current;
    token_t* tokens;
    struct{
        int16_t stack[MAX_STACK];
        uint8_t pos;
    }stack;
    float brightness;    // brightness from 0 -> 1
    rgb12 base_color;    // the original color value
    rgb12 color;         // the led setting algorythm used this color,
                         // it is pre calculated every tick
    bool visible;
}pattern_base_t;

typedef struct _tlc5947_tlc5947_obj_t{
    // base represents some basic information, like type
    mp_obj_base_t base;

    const spi_t*     spi;     // spi peripheral to use
    const pin_obj_t* blank;   // blank high -> all outputs off
    const pin_obj_t* xlat;    // low -> high transition GSR shift

    uint8_t buffer[36];       // buffer for the led colors
    uint8_t id_map[8];

    struct{
        /**
         * This is the list of all currently used patterns.
         * The patterna are advanced by every __call__
         * in the pattern_base_t the current color is stored,
         * this way the led update has to only get the color
         * from the corresponding pattern entry
         */
        struct{
            uint8_t len;          // length of the pattern list
            uint16_t pid;         // current pattern id (next pid = current pid + 1)
            pattern_base_t* list; // list of currently used patterns
        }patterns;

        /**
         * This is the pattern_map it maps the led's to the patterns,
         * in pattern_map[n].map[pattern_map[n].len - 1] there is the id of
         * the currently active pattern for led(n), this way a pattern
         * can be used by multiple led's and the patterns can also be
         * overwritten and deleted reliably.
         */
        struct{
            uint8_t len;          // length of the current pattern stack
            uint16_t* map;        // pattern stack, mapping patterns to leds
        }pattern_map[8];
        bool changed;
    }data;
    volatile uint8_t lock;
}tlc5947_tlc5947_obj_t;

#define LOCK(self)         do{(self)->lock++;                    }while(0)
#define UNLOCK(self)       do{if(IS_LOCKED((self))) (self)->lock--;}while(0)
#define IS_LOCKED(self)    ((self)->lock)
#define IS_UNLOCKED(self)  (!(self)->lock)

// tlc5947 buffer functions
static bool get_led_from_id_map(tlc5947_tlc5947_obj_t* self, uint8_t led_in, uint8_t* led);
static void set_buffer(uint8_t* buf, int led, rgb12 c);
static rgb12 get_buffer(uint8_t* buf, int led);


mp_obj_t tlc5947_tlc5947_make_new(const mp_obj_type_t *type, size_t n_args,
                                  size_t n_kw, const mp_obj_t *args);
STATIC void tlc5947_tlc5947_print(const mp_print_t *print,
                                  mp_obj_t self_in, mp_print_kind_t kind);
STATIC void* tlc5947_tlc5947_call(void* self_in, size_t _0, size_t _1, void* const* _2);
STATIC mp_obj_t tlc5947_tlc5947_blank(mp_obj_t self_in, mp_obj_t val);
STATIC mp_obj_t tlc5947_tlc5947_set(mp_obj_t self_in, mp_obj_t led_in, mp_obj_t pattern_in);
STATIC mp_obj_t tlc5947_tlc5947_replace(mp_obj_t self_in, mp_obj_t pid_in, mp_obj_t pattern_in);
STATIC mp_obj_t tlc5947_tlc5947_delete(mp_obj_t self_in, mp_obj_t pattern_in);
STATIC mp_obj_t tlc5947_tlc5947_get(mp_obj_t self_in, mp_obj_t led_in);
STATIC mp_obj_t tlc5947_tlc5947_exists(mp_obj_t self_in, mp_obj_t pid_in);
STATIC mp_obj_t tlc5947_tlc5947_set_id_map(mp_obj_t self_in, mp_obj_t map_in);

STATIC MP_DEFINE_CONST_FUN_OBJ_2(tlc5947_tlc5947_blank_obj, tlc5947_tlc5947_blank);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(tlc5947_tlc5947_set_obj, tlc5947_tlc5947_set);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(tlc5947_tlc5947_replace_obj, tlc5947_tlc5947_replace);
STATIC MP_DEFINE_CONST_FUN_OBJ_2(tlc5947_tlc5947_delete_obj, tlc5947_tlc5947_delete);
STATIC MP_DEFINE_CONST_FUN_OBJ_2(tlc5947_tlc5947_get_obj, tlc5947_tlc5947_get);
STATIC MP_DEFINE_CONST_FUN_OBJ_2(tlc5947_tlc5947_exists_obj, tlc5947_tlc5947_exists);
STATIC MP_DEFINE_CONST_FUN_OBJ_2(tlc5947_tlc5947_set_id_map_obj,tlc5947_tlc5947_set_id_map);

STATIC const mp_rom_map_elem_t tlc5947_tlc5947_locals_dict_table[] = {
    // class methods
    { MP_ROM_QSTR(MP_QSTR_blank),      MP_ROM_PTR(&tlc5947_tlc5947_blank_obj)      },
    { MP_ROM_QSTR(MP_QSTR_set),        MP_ROM_PTR(&tlc5947_tlc5947_set_obj)        },
    { MP_ROM_QSTR(MP_QSTR_replace),    MP_ROM_PTR(&tlc5947_tlc5947_replace_obj)    },
    { MP_ROM_QSTR(MP_QSTR_delete),     MP_ROM_PTR(&tlc5947_tlc5947_delete_obj)     },
    { MP_ROM_QSTR(MP_QSTR_get),        MP_ROM_PTR(&tlc5947_tlc5947_get_obj)        },
    { MP_ROM_QSTR(MP_QSTR_exists),     MP_ROM_PTR(&tlc5947_tlc5947_exists_obj)     },
    { MP_ROM_QSTR(MP_QSTR_set_id_map), MP_ROM_PTR(&tlc5947_tlc5947_set_id_map_obj) },
};
STATIC MP_DEFINE_CONST_DICT(tlc5947_tlc5947_locals_dict,tlc5947_tlc5947_locals_dict_table);


const mp_obj_type_t tlc5947_tlc5947_type = {
    // "inherit" the type "type"
    { &mp_type_type },
    // give it a name
    .name = MP_QSTR_tlc5947,
    // give it a print-function
    .print = tlc5947_tlc5947_print,
    // give it a constructor
    .make_new = tlc5947_tlc5947_make_new,
    // give it a call object
    .call = tlc5947_tlc5947_call,
    // and the global members
    .locals_dict = (mp_obj_dict_t*)&tlc5947_tlc5947_locals_dict,
};


mp_obj_t tlc5947_tlc5947_make_new(const mp_obj_type_t *type,
                                  size_t n_args,
                                  size_t n_kw,
                                  const mp_obj_t *args){
    mp_arg_check_num(n_args, n_kw, 3, 3, true);

    tlc5947_tlc5947_obj_t *self = m_new_obj(tlc5947_tlc5947_obj_t);

    self->base.type = &tlc5947_tlc5947_type;

    self->spi   = spi_from_mp_obj(args[0]);
    self->xlat  = pin_find(args[1]);
    self->blank = pin_find(args[2]);

    memset(self->buffer, 0, 36);
    memset(&self->data, 0, sizeof(self->data));
    self->data.changed = true; // make sure all leds are set to BLACK on startup

    // setup the default id_map
    for(uint16_t i = 0; i < 8; i++)
        self->id_map[i] = i;

    return MP_OBJ_FROM_PTR(self);
}

STATIC void tlc5947_tlc5947_print(const mp_print_t *print,
                                  mp_obj_t self_in,mp_print_kind_t kind){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "tlc5947(xlat=%d:%d, blank=%d:%d, lenght=%d)",
              self->xlat->port, self->xlat->pin, self->blank->port, self->blank->pin);
}


#if 0

#define m_malloc(size)                                  \
    ({                                                  \
        void * ptr;                                     \
        printf("m_malloc: %d\n\r", (unsigned int)size); \
        ptr = m_malloc(size);                           \
        printf("m_malloc: %d, 0x%08x\n\r",              \
               (unsigned int)size, (unsigned int)ptr);  \
        ptr;                                            \
    })

#define m_malloc_maybe(size)                            \
    ({                                                  \
        void * ptr;                                     \
        ptr = m_malloc_maybe(size);                     \
        printf("m_malloc: %d, 0x%08x\n\r",              \
               (unsigned int)size, (unsigned int)ptr);  \
        ptr;                                            \
    })

#define m_realloc_maybe(ptr, size, move)                        \
    ({                                                          \
        void * n_ptr;                                           \
        n_ptr = m_realloc_maybe(ptr, size, move);               \
        printf("m_realloc_maybe: 0x%08x, %d, %s, 0x%08x\n\r",   \
               (unsigned int)ptr, (unsigned int)size,           \
               move ? "true" : "false", (unsigned int)n_ptr);   \
        n_ptr;                                                  \
    })

#define m_free(ptr)                                             \
    ({                                                          \
        printf("m_free: 0x%08x\n\r", (unsigned int)ptr);        \
        m_free(ptr);                                            \
    })

#endif


#if 0
#define dprintf(f_, ...) printf((f_), ##__VA_ARGS__)
#else
#define dprintf(f, ...)
#endif

void dump_pattern(pattern_base_t* pattern){
    dprintf("\033[31m");
    dprintf("pattern dump:\n\r"
            "color: R: %02x G: %02x B: %02x\n\r"
            "current: %d\n\r"
            "id: %d\n\r"
            "len: %d\n\r"
            "stack.pos: %d\n\r",
            pattern->color.r,pattern->color.g,pattern->color.b,
            (unsigned int)pattern->current,
            pattern->id,
            (unsigned int)pattern->len,
            pattern->stack.pos
        );

    for(uint16_t i = 0; i < pattern->len; i++){
        switch(pattern->tokens[i].type){
        case pCOLOR:      {dprintf("pCOLOR\n\r");     break;}
        case pTRANSPARENT:{dprintf("pTRANSPARENT");   break;}
        case pSLEEP:      {dprintf("pSLEEP\n\r");     break;}
        case pBRIGHTNESS: {dprintf("pBRIGHTNESS\n\r");break;}
        case pINCREMENT:  {dprintf("pINCREMENT\n\r"); break;}
        case pDECREMENT:  {dprintf("pDECREMENT\n\r"); break;}
        case pFOREVER:    {dprintf("pFOREVER\n\r");   break;}
        case pJUMP_NZERO: {dprintf("pJNZ\n\r");       break;}
        case pMARK:       {dprintf("pMARK\n\r");      break;}
        case pPUSH:       {dprintf("pPUSH\n\r");      break;}
        case pPOP:        {dprintf("pPOP\n\r");       break;}
        default:          {dprintf("pDEFAULT\n\r");   break;}
        }
    }
    dprintf("\033[0m");
}

void dump_pattern_map(tlc5947_tlc5947_obj_t* self){
    dprintf("dump_pattern_map:\n\r");
    for(size_t i = 0; i < 8; i++){
        dprintf("led %d, len = %d:", (unsigned)i, self->data.pattern_map[i].len);
        for(size_t j = 0; j < self->data.pattern_map[i].len; j++){
            dprintf(" %d", self->data.pattern_map[i].map[j]);
        }
        dprintf("\n\r");
    }
}

#if 0
#define tprintf(f_, ...) dprintf((f_), ##__VA_ARGS__)
#else
#define tprintf(f, ...)
#endif

float clamp(float d, float min, float max) {
    const float t = d < min ? min : d;
    return t > max ? max : t;
}

// returns true if the pattern is done
static bool pattern_do_tick(tlc5947_tlc5947_obj_t* self, pattern_base_t* pattern){
    while(true){
        token_t* p = &pattern->tokens[pattern->current];
        switch(p->type){
        case pCOLOR:{      // change color
            tprintf("pCOLOR\n\r");
            pattern->base_color = pattern->color = p->color.color;
            pattern->brightness = 1.0F;
            self->data.changed = true;
            pattern->current++;
            if(pattern->current == pattern->len)
                return true; // pattern is done, no more tokens
            continue;
        }

        case pTRANSPARENT:{
            tprintf("pTRANSPARENT\n\r");
            pattern->visible = !pattern->visible;
            self->data.changed = true;
            pattern->current++;
            if(pattern->current == pattern->len)
                return true; // pattern is done, no more tokens
            continue;
        }

        case pSLEEP:{      // sleep for x amount of ticks
            tprintf("pSLEEP\n\r");
            if(!p->sleep.remaining){
                p->sleep.remaining = p->sleep.sleep_time;
            }else{
                p->sleep.remaining--;
                if(!p->sleep.remaining){
                    pattern->current++;
                    if(pattern->current == pattern->len)
                        return true; // pattern is done, no more tokens
                    continue;
                }
            }
            return false;
        }

        case pBRIGHTNESS:{ // change overall brightness
            tprintf("pBRIGHTNESS\n\r");
            self->data.changed = true;

            pattern->brightness = clamp(pattern->brightness + p->brighness.brightness, 0.0F, 1.0F);

            pattern->color.r = (uint16_t)((float)pattern->base_color.r * pattern->brightness);
            pattern->color.g = (uint16_t)((float)pattern->base_color.g * pattern->brightness);
            pattern->color.b = (uint16_t)((float)pattern->base_color.b * pattern->brightness);

            pattern->current++;
            if(pattern->current == pattern->len)
                return true; // pattern is done, no more tokens
            continue;
        }

        case pINCREMENT:{  // increment current stack value
            tprintf("pINCREMENT\n\r");
            pattern->stack.stack[pattern->stack.pos]++;
            pattern->current++;
            if(pattern->current == pattern->len)
                return true; // pattern is done, no more tokens
            continue;
        }

        case pDECREMENT:{  // decrement current stack value
            tprintf("pDECREMENT\n\r");
            pattern->stack.stack[pattern->stack.pos]--;
            pattern->current++;
            if(pattern->current == pattern->len)
                return true; // pattern is done, no more tokens
            continue;
        }

        case pFOREVER:{  // stay here for ever
            tprintf("pFOREVER\n\r");
            return false;
        }

        case pJUMP_NZERO:{ // jump to the matching marker if stack value is not 0
            tprintf("pJNZ\n\r");
            if(pattern->stack.stack[pattern->stack.pos]){
                pattern->current = p->jump.new_pp;
                return false;
            }else{
                pattern->current++;
                if(pattern->current == pattern->len)
                    return true; // pattern is done, no more tokens
                continue;
            }
        }

        case pMARK:{       // Marker for jump
            tprintf("pMARK\n\r");
            pattern->current++;
            continue;
        }

        case pPUSH:{       // Push value onto the stack
            tprintf("pPUSH\n\r");
            pattern->stack.pos++;
            if(pattern->stack.pos == MAX_STACK) // stack overflow
                return true;
            pattern->stack.stack[pattern->stack.pos] = p->push.value;
            pattern->current++;
            continue;
        }

        case pPOP:{         // Pop value from the stack
            tprintf("pPOP\n\r");
            if(!pattern->stack.pos) // stack underflow
                return true;
            pattern->stack.pos--;
            pattern->current++;
            continue;
        }

        default:
            tprintf("pDEFAULT --- ERROR\n\r");
            dump_pattern(pattern);
            return true;
        }
    }
}

/**
 * deletes a pattern from the pattern_list, and removes
 * all references to it in the pattern_map
 */
static bool delete_pattern(tlc5947_tlc5947_obj_t* self, uint16_t pid){
    dprintf("delete_pattern(%d)\n\r", pid);

    self->data.changed = true;

    // first delete all references in the pattern map
    for(uint16_t i = 0; i < 8; i++){ // itterate over all 8 pattern maps (i)
        if(self->data.pattern_map[i].map){ // if this pattern map exists
            for(uint16_t j = 0; j < self->data.pattern_map[i].len; j++){ // itterate over all id's in this map
                if(self->data.pattern_map[i].map[j] == pid){ // if this id matches the to be deleted id

                    LOCK(self);

                    self->data.pattern_map[i].len--; // remove one element from the list,
                                                     // do not de-allocate anything here this is done with
                                                     // realloc in the .set() led method

                    if(!self->data.pattern_map[i].len){
                        m_free(self->data.pattern_map[i].map);
                        self->data.pattern_map[i].map = NULL;
                        UNLOCK(self);
                        break;
                    }

                    memmove(&self->data.pattern_map[i].map[j], &self->data.pattern_map[i].map[j + 1],
                            (self->data.pattern_map[i].len - j) * sizeof(uint16_t));

                    UNLOCK(self);
                }
            }
        }
    }

    // now delete the pattern in the pattern list
    for(uint16_t i = 0; i < self->data.patterns.len; i++){
        if(self->data.patterns.list[i].id == pid){

            LOCK(self);

            // deallocate the token list
            m_free(self->data.patterns.list[i].tokens);
            self->data.patterns.list[i].tokens = NULL;

            self->data.patterns.len--;

            if(!self->data.patterns.len){
                m_free(self->data.patterns.list);
                self->data.patterns.list = NULL;
                UNLOCK(self);
                return true;
            }

            memmove(&self->data.patterns.list[i], &self->data.patterns.list[i+1],
                    sizeof(pattern_base_t) * (self->data.patterns.len - i));

            UNLOCK(self);

            dump_pattern_map(self);

            return true; // pattern id's are unique
        }
    }

    UNLOCK(self);

    return false;
}

static const rgb12 BLACK = {.r = 0, .g = 0, .b = 0};
static bool do_tick(tlc5947_tlc5947_obj_t* self){
    // first update all patterns, and delete finished patterns
    if(self->data.patterns.list){
        for(uint16_t i = 0; i < self->data.patterns.len; i++){
            if(pattern_do_tick(self, &self->data.patterns.list[i])){
                delete_pattern(self, self->data.patterns.list[i].id);
            }
        }
    }

    if(self->data.changed){
        // now that all patterns are updated, get the latest of all patterns and update the led buffer
        for(uint16_t led = 0; led < 8; led++){
            rgb12 color = BLACK;

            // find the matching pattern
            if(self->data.pattern_map[led].map){
                // current pid for this led
                uint16_t pid_pos = self->data.pattern_map[led].len-1;

                bool done = false;
                while(!done){
                    uint16_t pid = self->data.pattern_map[led].map[pid_pos];
                    for(uint16_t j = 0; j < self->data.patterns.len; j++){
                        if(self->data.patterns.list[j].id == pid){
                            color = self->data.patterns.list[j].color;

                            if(self->data.patterns.list[j].visible || (pid_pos == 0))
                                done = true;
                            --pid_pos;
                            break;
                        }
                    }
                }
            }

            set_buffer(self->buffer, led, color);
        }
    }
    return self->data.changed;
}

STATIC void* tlc5947_tlc5947_call(void* self_in, size_t _0, size_t _1, void* const* _2){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if(IS_UNLOCKED(self)){
        if(do_tick(self)){
            mp_hal_pin_low(self->xlat);
            spi_transfer(self->spi, 36, self->buffer, NULL, 100);
            mp_hal_pin_high(self->xlat);
            self->data.changed = false;
        }
    }
    return mp_const_none;
}

STATIC mp_obj_t tlc5947_tlc5947_blank(mp_obj_t self_in, mp_obj_t val){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_hal_pin_write(self->blank, mp_obj_is_true(val));

    return mp_const_none;
}

/**
 * This function checks if all the jumps ([]) in the string are balanced.
 * If this is not the case, this pattern is invalid
 */
static bool check_balanced_jumps(const char* s){
    int bc = 0;

    while(*s){
        switch(*s++){
        case '[':
            bc++;
            break;

        case ']':
            if(!bc) // there was no matching opening bracet
                return false;
            bc--;
            break;

        default:
            break;
        }
    }

    if(bc) // to many opening bracets
        return false;
    return true;
}

static inline int isdigit(int c){return ((c>='0')&&(c<='9'));}
static inline int isxdigit(int c){return (isdigit(c) || ((c>='A')&&(c<='F')) || ((c>='a')&&(c<='f')));}

/**
 * This function checks that all colors used
 * in this string are valid RGB or HSV colors.
 */
static bool check_colors(const char* s){
    while(*s){
        switch(*s++){
        case '#': // check that the next 6 characters are hexadecimal digits
            for(uint16_t i = 0; i < 6; i++)
                if(!isxdigit(s[i]))
                    return false;
            break;
        case '$':// TODO
            break;
        default:
            break;
        }
    }
    return true;
}

static size_t get_pattern_length(const char* s){
    size_t len = 0;
    while(*s){
        switch(*s++){
        case '#':
            len++;
            s += 6;
            break;

        case '$':
            len++;
            s += 11;
            break;

        case '\b':
            while(isdigit(*s) || (*s == '.') || (*s == '-'))
                s++;
            len++;
            break;

        case '|':
        case '<':
            while(isdigit(*s))
                s++;

            // simple tokens(no arguments)
        case '[':
        case ']':
        case '+':
        case '-':
        case ';':
        case '@':
        case '>':
            len++;
            break;

        default:
            return 0;
        }
    }
    return len;
}

static int atoi(const char *p) {
    int k = 0;
    while(((*p>='0')&&(*p<='9'))){
        k = (k << 3) + (k << 1) + (*p) - '0';
        p++;
    }
    return k;
}

float atof(const char *s){
    // This function stolen from either Rolf Neugebauer or Andrew Tolmach.
    // Probably Rolf.
    float a = 0.0;
    int e = 0;
    int c;
    bool sign = false;

    if(*s == '-'){
        s++;
        sign = true;
    }

    while((c = *s++) != '\0' && isdigit(c)){
        a = a * 10.0f + (c - '0');
    }

    if(c == '.'){
        while((c = *s++) != '\0' && isdigit(c)){
            a = a * 10.0f + (c - '0');
            e = e-1;
        }
    }

    if(c == 'e' || c == 'E'){
        int sign = 1;
        int i = 0;
        c = *s++;
        if(c == '+')
            c = *s++;
        else if(c == '-'){
            c = *s++;
            sign = -1;
        }
        while(isdigit(c)){
            i = i*10 + (c - '0');
            c = *s++;
        }
        e += i*sign;
    }

    while(e > 0){
        a *= 10.0f;
        e--;
    }

    while(e < 0){
        a *= 0.1f;
        e++;
    }
    return sign ? -a : a;
}

static void tokenize_pattern_str(const char* s, token_t* pat, size_t len){
    size_t i = 0;
    dprintf("parse start:\n\r");
    while(*s && (len > i)){
        switch(*s++){
        case '#':
            dprintf("RGB COLOR\n\r");
            pat[i].type = pCOLOR;
            pat[i].color.color = get_rgb12(--s);
            s += 7;
            break;

        case '$':
            dprintf("HSV COLOR\n\r");
            pat[i].type = pCOLOR;
            // TODO: implement HSV color
            mp_raise_NotImplementedError(MP_ERROR_TEXT("HSV color"));
            break;

        case '@':
            dprintf("TRANSPARENT\n\r");
            pat[i].type = pTRANSPARENT;
            break;

        case '\b':{
            dprintf("BRIGHTNESS\n\r");
            pat[i].type = pBRIGHTNESS;
            int len = 0;
            if(*s == '-')
                len++;
            while(isdigit(s[len]) || (s[len] == '.'))
                len++;
            pat[i].brighness.brightness = atof(s);
            s += len;
            break;
        }

        case '|':{
            dprintf("SLEEP\n\r");
            pat[i].type = pSLEEP;
            int len = 0;
            while(isdigit(s[len]))
                len++;
            pat[i].sleep.sleep_time = atoi(s);
            pat[i].sleep.remaining = 0;
            s += len;
            break;
        }

        case '<':{
            pat[i].type = pPUSH;
            int len = 0;
            while(isdigit(s[len]))
                len++;
            pat[i].push.value = atoi(s);
            s += len;
            dprintf("PUSH %d\n\r", (int)pat[i].push.value);
            break;
        }

        case '>':
            dprintf("POP\n\r");
            pat[i].type = pPOP;
            break;

        case '[':
            dprintf("MARK\n\r");
            pat[i].type = pMARK;
            break;

        case ']':{
            pat[i].type = pJUMP_NZERO;
            int jc = 0;
            bool f = true;
            for(size_t j = i; j && f; j--){
                switch(pat[j].type){
                case pJUMP_NZERO:
                    jc++;
                    break;
                case pMARK:
                    jc--;
                    if(!jc){
                        pat[i].jump.new_pp = j;
                        f = 0;
                    }
                    break;
                default:
                    break;
                }
            }
            dprintf("JNZ %d\n\r", (int)pat[i].jump.new_pp);
            break;
        }

        case '+':
            dprintf("INCREMENT\n\r");
            pat[i].type = pINCREMENT;
            break;

        case '-':
            dprintf("DECREMENT\n\r");
            pat[i].type = pDECREMENT;
            break;

        case ';':
            dprintf("LOOP FOREVER\n\r");
            pat[i].type = pFOREVER;
            return; // we are done

        default:
            printf("\n\r !!! parsing error !!! \n\r");
            break;
        }
        i++;
    }
    dprintf("parse done\n\r");
}

STATIC mp_obj_t tlc5947_tlc5947_set(mp_obj_t self_in, mp_obj_t led_in, mp_obj_t pattern_in){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // lex the current pattern, and add it to the pattern_list
    const char* pattern_str = mp_obj_str_get_str(pattern_in);

    if(!check_balanced_jumps(pattern_str)){
        mp_raise_msg(&mp_type_AttributeError, MP_ERROR_TEXT("unbalanced jumps"));
    }

    if(!check_colors(pattern_str)){
        mp_raise_msg(&mp_type_AttributeError, MP_ERROR_TEXT("invalid color Format"));
    }

    size_t pl = get_pattern_length(pattern_str);
    if(!pl){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid Pattern"));
    }

    /**
     * Lock the tlc5947_object, because if realloc succeds the original pattern list becomes invalid
     * this makes sure the pattern list is not used by __call__
     */
    LOCK(self);

    void* new_plist = m_realloc_maybe(self->data.patterns.list,
                                      sizeof(pattern_base_t) * (self->data.patterns.len+1), true);
    if(!new_plist){
        UNLOCK(self);
        m_malloc_fail(sizeof(token_t) * (self->data.patterns.len+1));
    }
    self->data.patterns.list = new_plist;

    // get a new pattern ID
    uint16_t pid = ++self->data.patterns.pid;

    memset(&self->data.patterns.list[self->data.patterns.len], 0, sizeof(pattern_base_t));

    self->data.patterns.list[self->data.patterns.len].tokens  = m_malloc_maybe(sizeof(token_t) * pl);
    self->data.patterns.list[self->data.patterns.len].id      = pid;
    self->data.patterns.list[self->data.patterns.len].len     = pl;
    self->data.patterns.list[self->data.patterns.len].visible = true;

    if(!self->data.patterns.list[self->data.patterns.len].tokens){
        UNLOCK(self);
        m_malloc_fail(sizeof(token_t) * pl);
    }

    tokenize_pattern_str(pattern_str, self->data.patterns.list[self->data.patterns.len].tokens, pl);

    self->data.patterns.len++;

    /**
     * The new pattern list has been set, the interrupt can operate on it again
     */
    UNLOCK(self);

    // now put this new pattern into the pattern_map
    if(mp_obj_is_int(led_in)){
        int tmpled;
        if(!mp_obj_get_int_maybe(led_in, &tmpled)){
            delete_pattern(self, pid);
            mp_raise_TypeError(MP_ERROR_TEXT("expected int"));
        }
        uint8_t led;
        if(!get_led_from_id_map(self, mp_obj_get_int(led_in), &led)){
            delete_pattern(self, pid);
            mp_raise_ValueError(MP_ERROR_TEXT("led not in id_map"));
        }

        LOCK(self);

        void* new_map = m_realloc_maybe(self->data.pattern_map[led].map,
                                        sizeof(uint16_t) * (self->data.pattern_map[led].len + 1), true);
        if(!new_map){// realloc failed, delete pattern
            UNLOCK(self);
            delete_pattern(self, pid);
            m_malloc_fail(sizeof(uint16_t) * (self->data.pattern_map[led].len + 1));
        }
        self->data.pattern_map[led].map = new_map;

        self->data.pattern_map[led].map[self->data.pattern_map[led].len] = pid;
        self->data.pattern_map[led].len++;

        UNLOCK(self);

    }else if(mp_obj_is_type(led_in, &mp_type_list)){
        mp_obj_t* list;
        size_t len;
        mp_obj_get_array(led_in, &len, &list);
        int* leds = m_malloc(len * sizeof(int));

        for(size_t i = 0; i < len; i++){
            if(!mp_obj_get_int_maybe(list[i], &leds[i])){
                delete_pattern(self, pid);
                m_free(leds);
                mp_raise_TypeError(MP_ERROR_TEXT("expected list of int"));
            }
        }

        for(size_t i = 0; i < len; i++){
            uint8_t led;
            if(!get_led_from_id_map(self, leds[i], &led)){
                delete_pattern(self, pid);
                m_free(leds);
                mp_raise_TypeError(MP_ERROR_TEXT("led not in map"));
            }

            LOCK(self);

            void* new_map = m_realloc_maybe(self->data.pattern_map[led].map,
                                            sizeof(uint16_t) * (self->data.pattern_map[led].len + 1), true);
            if(!new_map){// realloc failed, delete pattern
                UNLOCK(self);
                delete_pattern(self, pid);
                m_malloc_fail(sizeof(uint16_t) * (self->data.pattern_map[led].len + 1));
            }
            self->data.pattern_map[led].map = new_map;

            self->data.pattern_map[led].map[self->data.pattern_map[led].len] = pid;
            self->data.pattern_map[led].len++;

            UNLOCK(self);
        }
        m_free(leds);
    }else{
        delete_pattern(self, pid);
        mp_raise_TypeError(MP_ERROR_TEXT("expected int or list of int"));
    }

    dump_pattern_map(self);

    return mp_obj_new_int(pid);
}

STATIC mp_obj_t tlc5947_tlc5947_replace(mp_obj_t self_in, mp_obj_t pid_in, mp_obj_t pattern_in){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);

    const char* pattern_str = mp_obj_str_get_str(pattern_in);

    if(!check_balanced_jumps(pattern_str)){
        mp_raise_msg(&mp_type_AttributeError, MP_ERROR_TEXT("unbalanced jumps"));
    }

    if(!check_colors(pattern_str)){
        mp_raise_msg(&mp_type_AttributeError, MP_ERROR_TEXT("invalid color Format"));
    }

    size_t pl = get_pattern_length(pattern_str);
    if(!pl){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid Pattern"));
    }

    int pid = mp_obj_get_int(pid_in);

    int pos = -1;
    for(uint16_t i = 0; i < self->data.patterns.len; i++)
        if(self->data.patterns.list[i].id == pid){
            pos = i;
            break;
        }

    if((pid <= 0) || (pos == -1))
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid Pattern ID"));

    token_t* new_tokens = m_malloc_maybe(sizeof(token_t) * pl);

    if(!new_tokens){
        m_malloc_fail(sizeof(token_t) * pl);
    }

    tokenize_pattern_str(pattern_str, new_tokens, pl);

    LOCK(self);

    m_free(self->data.patterns.list[pos].tokens);
    memset(&self->data.patterns.list[pos], 0, sizeof(pattern_base_t));

    self->data.patterns.list[pos].tokens  = new_tokens;
    self->data.patterns.list[pos].id      = pid;
    self->data.patterns.list[pos].len     = pl;
    self->data.patterns.list[pos].visible = true;

    UNLOCK(self);

    return mp_obj_new_int(pid);
}


STATIC mp_obj_t tlc5947_tlc5947_delete(mp_obj_t self_in, mp_obj_t pattern_in){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int pid = mp_obj_get_int(pattern_in);

    return mp_obj_new_bool(delete_pattern(self, pid));
}

static inline __attribute__((pure)) char gethex(uint8_t i){
    return "0123456789ABCDEF"[i];
}
STATIC mp_obj_t tlc5947_tlc5947_get(mp_obj_t self_in, mp_obj_t led_in){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t led;
    if(!get_led_from_id_map(self, mp_obj_get_int(led_in), &led)){
        mp_raise_ValueError(MP_ERROR_TEXT("led not in map"));
    }

    rgb8 c = rgb12torgb8(get_buffer(self->buffer, led));

    char* str = m_malloc(8);
    str[0] = '#';
    str[1] = gethex((c.r&0xF0)>>4);
    str[2] = gethex( c.r&0x0F);
    str[3] = gethex((c.g&0xF0)>>4);
    str[4] = gethex( c.g&0x0F);
    str[5] = gethex((c.b&0xF0)>>4);
    str[6] = gethex( c.b&0x0F);
    str[7] = 0;
    return mp_obj_new_str(str, 7);
}

STATIC mp_obj_t tlc5947_tlc5947_exists(mp_obj_t self_in, mp_obj_t pid_in){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if(!mp_obj_is_int(pid_in))
        return mp_const_false;

    int pid = mp_obj_get_int(pid_in);

    if(pid <= 0)
        return mp_const_false;

    for(uint16_t i = 0; i < self->data.patterns.len; i++)
        if(self->data.patterns.list[i].id == pid)
            return mp_const_true;

    return mp_const_false;
}

STATIC mp_obj_t tlc5947_tlc5947_set_id_map(mp_obj_t self_in, mp_obj_t map){
    tlc5947_tlc5947_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t *items;
    mp_obj_get_array_fixed_n(map, 8, &items);

    for(uint32_t i = 0; i < 8; i++){
        int j;
        if(mp_obj_get_int_maybe(items[i], &j)){
            if((j >= 0) && (j <= 8)){
                self->id_map[i] = j;
            }else if(j == -1){
                self->id_map[i] = 0xFF;
            }else{
                mp_raise_ValueError(MP_ERROR_TEXT("led out of range"));
            }
        }else{
            // failed to get int
            for(uint32_t k = 0; k < 8; k++)
                self->id_map[k] = k;
            mp_raise_TypeError(MP_ERROR_TEXT("can't convert to int"));
        }
    }
    return mp_const_none;
}


static bool get_led_from_id_map(tlc5947_tlc5947_obj_t* self, uint8_t led_in, uint8_t* led){
    if((led_in < 0) || (led_in >= 8))
        return false;
    if(self->id_map[led_in] == 0xFF)
        return false;

    *led = self->id_map[led_in];
    return true;
}

const uint8_t lut[] = {0,4,9,13,18,22,27,31};
static void set_buffer(uint8_t* buf, int led, rgb12 c){
    if(!(led % 2)){
        buf[lut[led]  ]  = (uint8_t)(c.b >> 4);
        buf[lut[led]+1]  = (uint8_t)(((c.b&0x0F) << 4) | ((c.g>>8)&0x0F));
        buf[lut[led]+2]  = (uint8_t)(c.g);
        buf[lut[led]+3]  = (uint8_t)(c.r >> 4);
        buf[lut[led]+4]  = (uint8_t)((c.r&0x0F)<<4)|(buf[lut[led]+4]&0x0F);
    }else{
        buf[lut[led]  ]  = (uint8_t)(((c.b>>8)&0x0F)|(buf[lut[led]]&0xF0));
        buf[lut[led]+1]  = (uint8_t)c.b;
        buf[lut[led]+2]  = (uint8_t)(c.g>>4);
        buf[lut[led]+3]  = (uint8_t)(((c.g&0x0F)<<4)|((c.r>>8)&0x0F));
        buf[lut[led]+4]  = (uint8_t)c.r;
    }
}

static rgb12 get_buffer(uint8_t* buf, int led){
    rgb12 c = {0};
    if(!(led % 2)){
        c.r = (buf[lut[led]+3]<<4) | ((buf[lut[led]+4]&0xF0)>>4);
        c.g = (buf[lut[led]+2]   ) | ((buf[lut[led]+1]&0x0F)<<8);
        c.b = (buf[lut[led]  ]<<4) | ((buf[lut[led]+1]&0xF0)>>4);
    }else{
        c.r = (buf[lut[led]+4])    | ((buf[lut[led]+3]&0x0F)<<8);
        c.g = (buf[lut[led]+2]<<4) | ((buf[lut[led]+3]&0xF0)>>4);
        c.b = (buf[lut[led]+1])    | ((buf[lut[led]  ]&0x0F)<<8);
    }
    return c;
}


STATIC const mp_rom_map_elem_t tlc5947_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_tlc5947)  },
    { MP_OBJ_NEW_QSTR(MP_QSTR_tlc5947),  MP_ROM_PTR(&tlc5947_tlc5947_type) },
};

STATIC MP_DEFINE_CONST_DICT(
    mp_module_tlc5947_globals,
    tlc5947_globals_table
    );

const mp_obj_module_t mp_module_tlc5947 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_tlc5947_globals,
};

MP_REGISTER_MODULE(MP_QSTR_tlc5947, mp_module_tlc5947, MODULE_TLC5947_ENABLED);

#endif /* defined(MODULE_TLC5947_ENABLED) && MODULE_TLC5947_ENABLED == 1 */
