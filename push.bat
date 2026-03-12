@echo off
cd /d "%~dp0"
git add -A
git commit -m "Move main.cpp to root for Wokwi compatibility"
git push
