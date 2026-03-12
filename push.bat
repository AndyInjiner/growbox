@echo off
cd /d "%~dp0"
git add -A
git commit -m "Add README with Wokwi badge"
git push
