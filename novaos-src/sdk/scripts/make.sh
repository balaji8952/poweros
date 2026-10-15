#!/bin/sh
# Builds all example apps and installs them into disk.img  (needs Python 3.8+)
cd "$(dirname "$0")/.." || exit 1
python3 build_all.py && python3 make_disk.py disk.img
