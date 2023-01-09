# Retro OPL2 emulator

This is a standalone software emulation of the OPL2 hardware found in AdLib and SoundBlaster sound cards, which is capable of FM synthesis.

The main program `retro_opl` is invoked like this:

    ./retro_opl output.wav 44100 < input.opl2

The first parameter `output.wav` is the path to the WAV file to create.  The WAV file will contain sound approximating what actual OPL2 hardware would produce.  The second parameter `44100` is the sampling rate to use in the WAV file.  You can choose either 44100 or 48000.

The program reads an OPL2 hardware script from standard input.  The script has a simple format like this:

    OPL2 60
    ' Comment lines start with an apostrophe
    ' The "60" in the header line is the control rate in Hz
    ' Control rates may be in range [1, 1024]
    
    ' To write value 0x87 into OPL2 register 0xa2:
    r a2 87

    ' To advance 60 cycles at the control rate:
    w 60

(No leading whitespace is allowed at the start of any non-blank line!)

A format specification of this hardware script format is available as part of the Retro specification in the [Retro project](https://github.com/canidlogic/retro).  However, the sample code above should be sufficient.  You just declare a control rate in Hz, and then give a sequence of OPL2 register write commands `r` along with wait commands `w` that produce sound using the current state of the registers.

## Sample OPL2 script

The famous "Programming the AdLib/Sound Blaster FM Music Chips" article written by Jeffrey S. Lee in 1992 gives a sample OPL2 hardware register configuration to produce a sound.  The following is an OPL2 hardware script that produces that sound for two seconds:

    OPL2 60
    r 20 01
    r 40 10
    r 60 f0
    r 80 77
    r a0 98
    r 23 01
    r 43 00
    r 63 f0
    r 83 77
    r b0 31
    w 120

This sample file is included in the distribution as `first.opl2`

## VGM support

You can get historic OPL2 music from [vgmrips.net](https://vgmrips.net/).  You can use any music that is written for the YM3812 chips.  However, you can not directly pass the VGM or VGZ files to `retro_opl`.  Instead, do the following:

1. If you have a compressed VGZ file, rename its extension from `.vgz` to `.vgm.gz` and then run `gunzip` on it to decompress it into a VGM file.
2. Run the **decompressed** VGM through the `vgm2opl` program included with the Retro OPL2 emulator to convert it into an OPL2 hardware script.
3. Pass the converted OPL2 hardware script into `retro_opl`.

The `vgm2opl` program included with this distribution has the following syntax:

    ./vgm2opl input.vgm 1 > output.opl2

The first parameter is the path to the _decompressed_ VGM file.  If you have a compressed VGZ file, you need to decompress it first.  The second parameter is either `1` to run the music once through, or `2` to loop through it twice, using any looping information present in the VGM file.  The OPL2 hardware script is written to standard output.

**Caveat:**  Timing conversion from VGM to OPL2 hardware script is not perfect.  It should be a good enough approximation, but it is not a perfect conversion.

**Caveat:**  `vgm2opl` can only handle VGM files for the OPL2/YM3812 chip.  Errors occur if the VGM has any opcodes relating to other chipsets.

## Build instructions

In order to build `retro_opl`, you need the DOSBox OPL emulator module.  You need the `opl.cpp` and `opl.h` source files from the `src/hardware` directory of the DOSBox source code.  Then, you need to edit those files so they compile by themselves, rename `opl.cpp` to `opl.c`, and edit it so it compiles as C instead of C++.

For your convenience, there is a [DOSBox-X fork here](https://github.com/canidlogic/dosbox-x) that has already done this work for you.  You just need to go to the `oplemu` directory in that fork and grab the `opl.c` and `opl.h` files, which have already been edited to compile as a standalone C module.

Once you have `opl.c` and `opl.h` copied into the same directory as the `retro_opl` source files, you can build `retro_opl` like this with GCC:

    gcc -O2 -o retro_opl retro_opl.c opl_driver_dosbox.c opl.c -lm

Building `vgm2opl` is even simpler:

    gcc -O2 -o vgm2opl vgm2opl.c -lm

Finally, test out the `retro_opl` program you just built using the included `first.opl2` script:

    ./retro_opl first.wav 44100 < first.opl2

The result should be a `first.wav` file that plays a simple synthesized tone for two seconds.
