# Pixter emulators + debuggers (with macOS changes)
TLDR (from [original website](https://dmitry.gr/?r=05.Projects&proj=37.%20Pixter)): First ever complete reverse engineering, documentation, emulation, and preservation of all Fisher-Price/Mattel Pixter device series and [almost] all the games.


This version has been edited to support compilation on macOS. Not all aspects have been fully tested. Changes should be non-breaking on Linux.

## Compilation Instructions:

### Compiling uARMpixter/uPixter:
Install gcc-arm-embedded and arm-none-eabi-binutils: `brew install --cask gcc-arm-embedded` and `brew install arm-none-eabi-binutils`, `make` in given directory.

### Compiling uM23:
Install cc65: `brew install cc65` and run `make` in the uM23 directory.

### Compiling ClassicDisasm/ColorDisasm/PalmosLauncherMulti:
`make` in given directory.

## License:
All code within this repository is GPLv3-licensed. The original source code can be found [here](https://dmitry.gr/images/pixter_Downloadables.tar.bz2). This repository does not distribute any ROMs or any code that is not licensed for public distribution.
