@echo off
rem Builds all example apps and installs them into disk.img  (needs Python 3.8+)
cd /d "%~dp0\.."
python build_all.py || py build_all.py
python make_disk.py disk.img || py make_disk.py disk.img
pause
