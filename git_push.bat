@echo off
cd /d "%~dp0"
git add -A
git commit -m "Fix wokwi.toml"
git push
