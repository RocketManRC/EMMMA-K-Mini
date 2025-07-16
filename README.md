# EMMMA-K-Mini

# Introduction
The EMMMA-K-Mini is a MIDI controller for electronic music that uses touch pins for note keys.

![Photo](images/IMG_2009.jpg)

This is a newer and smaller version of the previous EMMMA-K which can be found here:

[https://github.com/RocketManRC/EMMMA-K-V3-M](https://github.com/RocketManRC/EMMMA-K-V3-M)

# Differences From Previous Version

- Much more compact form factor.
- Uses a single ESP32-S3 microcontroller which makes it easier to build albeit 13 note pins instead of 17.
- Colour LCD display.
- Rotary encoder for user interface (UI) instead of touch pins.
- Default UI page is note velocity (volume).

# User Interface

## Startup Page

![Photo](images/IMG_2011.jpg)

This is showing the MIDI velocity (a.k.a. note volume). There are two settings for this to select a high and low volume. A single press on the rotary encoder toggles betweem the two. Rotating the encoder will change the value of the setting.

## Options Mode

A double press on the encoder will switch to Options mode. Rotate the encoder to select the option to change and then single press the endoder to change the option.

![Photo](images/IMG_2012.jpg)

The three options that can be changed are:
- Scale (22 variations).
- Key (C, C#, D, etc).
- Octave (relative to scale starting with MIDI value 60).

A single encoder press on the notes page will display the note being played. Single press again to get back to selecting the options page. 

A single encoder press on the master volume page will get back to the UI the way it was at startup (NOTE this will also have the effect of unintentionally toggling between the low and high velocities which is a fault that needs to be fixed).

## Config Mode

A long press when in Options Mode will change to Config Mode (NOTE this doesn't work when in Options Mode and displaying the Master Volume page. Another fault to be fixed!).

![Photo](images/IMG_2013.jpg)



