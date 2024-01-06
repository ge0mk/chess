#!/usr/bin/bash

convert -background transparent img/*.svg -resize 256x256 img/%d.png
montage img/*.png -background transparent -tile 8x1 spritesheet.png
