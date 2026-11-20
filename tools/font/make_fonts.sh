#!/bin/sh
# Rebuilds core/font_data.h. Run from the repository root.
F=assets/fonts/KF_Libron-Regular.ttf
{
  echo "/* Fonts baked into Nihil. See assets/fonts/README.md for where they come from. */"
  echo "#pragma once"
  python3 tools/font/gen_font.py $F 46 font_body 0.8
  python3 tools/font/gen_font.py $F 30 font_small 0.8
} > core/font_data.h
