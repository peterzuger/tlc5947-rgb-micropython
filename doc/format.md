# Pattern ID's
The pattern\_id is a unique positive non zero integer, when creating a
pattern with .set() a new pattern\_id is created and then belongs to
that pattern.


## Pattern to LED Mapping
The individual patterns are mapped to the led's via the *Pattern Map*
(internal name: pattern_map[8]).

This is a simple 2 dimensional array, the first dimension is currently
fixed at 8 elements one for each RGB LED on the TLC5947 device. It is
planned to make this dynamically expandable, this would allow you to
chain multiple TLC5947 devices in series.

The second dimension is a dynamic array of Integers, the last Integer
in each array is the pattern\_id of the currently active pattern.

When a new Pattern is created, via the tlc5947.set() method, the
pattern\_id corresponding to the pattern will be appended to the end
of the second dimension of the respective led's that are chosen with
the first parameter of the tlc5947.set() method.

The pattern itself containing all the data corresponding to its state
is held in the pattern list, this is a simple list containing all the
pattern in the order they are added. The individual patterns contain a
field for the pattern\_id, this is then used to do the mapping to the
LED's via the pattern Map.


# Pattern Format
## #<RR><GG><BB>      a color in RGB format
This token sets the current color of the LED's to the specified RGB
value. Be aware that this cannot be used on its own, see the examples.


### Examples
This is a simple set color example:
```python
tlc.set(1, "#FFFF00;") # Set LED 1 permanently to Yellow
```

It is also possible to use dynamic colors:
```python
color = input("Input an RGB color")
try:
    tlc.set(1, "{};".format(color)) # Set LED 1 permanently to the user supplied color
except:
    pass
```


## $<Hue>,<Sat>,<Val> a color in HSV format
This token has the same effect as the previous token, but takes its
input in HSV form. See
[here](https://en.wikipedia.org/wiki/HSL_and_HSV) for more info.


### Examples
Another simple set color example:
```python
tlc.set(1, "60,1,1;") # Set LED 1 permanently to Yellow
```


## |<n>               sleep for n ticks
This is a delay, that can be used to implement more elaborate color
profiles. The usage of this token is best illustrated in the Examples.

The delay n can range anywhere from 0 to 65535 (2**16-1). If a delay
above this is chosen the integer internally representing the delay
will overflow and the behavior is undefined.

The delay is in so called `tick`s, for an explanation of ticks see
[this](tlc5947.md).


### Examples
This sets LED 1 to Red then is waits for 50 ticks and it changes the
color to Green.
```
tlc.set(1, "#FF0000|50#00FF00;")
```


## \b<n>             change the brightness by n
This token changes the brightness of the color currently in use to by
the specified amount n. This change is done to the color in the HSV
spectrum. This means the color is converted to HSV and the given value
(wich can be positive or negative) is added to the Value portion of
the HSV color. With a negative value, the brightness can be decreased.

The value has to be within the range -1 <= value <= 1, if the value
added added to the current value exceeds the range of the HSV spectrum
0 <= V <= 1 it is automatically truncated.


### Examples
This sets LED 1 to Yellow then is waits for 50 ticks and it changes the
brightness to 50%.
```
tlc.set(1, "#FF0000|50\b-0.5;")
```


## <<n>               push n onto the stack
Each pattern, in its own data, has a stack it is a fixed 10 element
array. This token can be used to push a value onto this stack. Wich
means incrementing the stack pointer and setting the top of the stack
to the specified value.

The stack is only useful together with the `[` and `]` tokens, see
their example section for examples of the stack operators.


## >                  pop a value from the stack
This token can be used to pop a value of the stack. The value is not
really deleted from the stack, it only decrements the stack pointer
since the only way of incrementing the stack pointer is pushing a new
value with the previous token.


## +                  increment current stack value
This increments the value that is currently at the top of the stack.
Its currently only used for creating an infinite loop together with JNZ and the marker.


### Examples
This is an infinite loop, that changes the led from "white"(#FFFFFF)
to "black"(#000000) every 500 ticks. This works by first incrementing
the initial stack value from 0 to 1, then it sets up a marker for the
JNZ to jump to then it sets the led to white, sleeps for 500 ticks and
then sets the color to black and sleeps for 500 ticks again. Because
the stack value is not 0 it then jumps to the Marker and the cycle
repeats. Since the stack value is never decreased again, this pattern
will continue forever.
```python
tlc.set(1, "+[#FFFFFF|500#000000|500]")
```


## -                  decrement current stack value
This is the same as the increment token except that it (obviously)
decrements the value currently at the top of the stack. This has has a
lot more uses than the increment token. This can be used to get things
similar to for loops, which in turn can be nested using the stack
push/pop feature.


### Examples
This is the same loop from the previous example, but this time a value
is pushed onto the stack instead of just incrementing the value from
0. And this value is then at the end of the loop decremented. Once
this value reaches 0, in this case after 5 iterations, the loop will
terminate and the pattern will be finished.
```python
tlc.set(1, "<5[#FFFFFF|500#000000|500-]")
```


## ]                  Jump to the matching marker if stack value is not 0
This token can be used to jump to a matching `[` marker if some
conditions are met. It works the same as the ']' character in
[Brainfuck](https://en.wikipedia.org/wiki/Brainfuck), except that the
check is inverted.


### Examples
This example sets LED 1 to Red and then in 10 steps decreases the
brightness to 0.
```python
tlc.set(1, "#FF0000<10[\b-0.1|10]")
```

This sets LED 1 to Yellow it then in a loop decreases the brightness
to 50% in 50 ticks and after this increases the brightness to 100%
again in 50 ticks. This will create a triangle wave going from 100%
brightness to 50% and back.
```python
tlc.set(1, "#FFFF00+[<20[\b-0.04|2-]><20[\b0.04|2-]>]")
```


## [                  Marker
This token is used together with JNZ see above.


## ;                  loop forever(infinite delay)
This is one of the tokens that has been used in most examples before
this without being explained, but it is one of the most important
especially for simple patterns.

This token acts the same as the delay explained before, but with an
infinite delay. This is useful because once a pattern has been
completely processed (no more tokens left). The pattern will end and
be removed from the pattern list and all corresponding LED mappings.

This simple token, if placed at the end can be used to ensure the
pattern stays active until manually removed.


### Examples
Here we go back to the first and most basic example, the permanent Yellow LED.
This would not be possible without this token.
```
tlc.set(1, "#FFFF00;")
```

This is the same as above but without the loop forever, and this will
not do anything. Because of the way colors in the patterns are
calculated, this will set it's internal color to Yellow and then
immediately terminate and be removed. This means the LED will never be
Yellow.
```
# This does not work
#tlc.set(1, "#FFFF00")
```


## @                  toggle the transparency
Since it is not possible to rearrange the pattern map without deleting
and recreating it, there has to be a way of making a pattern that is
lower in the pattern map active. This token can help with this.

This token toggles a parameter that is internal to each pattern, its
transparency. When a pattern is transparent, it is not considered for
the mapping of LED to pattern.

When the mapping the LED's to the patterns, usually the color value
stored in pattern with the id stored in the top element of the stack
of each LED is taken and put in the buffer that will be written to the
greyscale registers of the tlc5947. If the pattern has the transparent
flag set, which can be set with this token, the pattern is not
considered and the pattern below the current one is checked to which
the same procedure is applied.


### Examples
This example will illustrate how this can be used to layer patterns on
top of each other.
```python
tlc.set(1, "#FFFF00;") # Set the LED to Yellow
p = tlc.set(2, "@;")   # Add another pattern on top
                       # this pattern is purely transparent

# At this point the LED is still Yellow

tlc.replace(p, "#0000FF|50@;") # Set the LED to Blue
                               # Wait for 50 ticks
                               # and make the pattern transparent again.

# 50 ticks later...
# The LED is Yellow Again
# And this all without the second pattern
# knowing the color of the first pattern.
```
