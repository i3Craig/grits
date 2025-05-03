#### OVERVIEW
Grits is a Virtual Globe library developed along side AWeather, but desigend to
be usable by other program as well.
- Current version: 0.9.0

It is differentiated from other Virtual Globes such as Google Earth, NASA World
Wind, and KDE Marble in that it is developed primairily as a library that is
used by other programs, rather than providing it's own user interface.

For more information, see the homepage:
  - http://pileus.org/grits

#### SYSTEM REQUIREMENTS
- OpenGL 2.1+ or OpenGL ES 2.1+
  Note: Systems with OpenGL 3.1 or newer will perform better.

#### CHANGES
This repo is a clone of http://pileus.org/grits/files/grits-0.8.1.tar.gz with updates shown below:
- November 2024 - Updated ground level rendering so that the state borders don't look like they are floating in the sky.
- January 2025 - Added the grits_volume_set_level_sync function so the 3D radar volume doesn't "glitch" when the ISO level is adjusted during the animation.
- May 2025 - Added support for OpenGL ES 2.1+, improved performance (FPS) when moving the map around.
