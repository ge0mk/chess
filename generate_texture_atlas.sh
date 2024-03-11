#!/usr/bin/bash

convert -background transparent textures/*.svg -resize 256x256 build/%d.png
montage build/*.png -background transparent -tile 8x1 textures/spritesheet.png
