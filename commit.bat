@echo off
cd /d "%~dp0"
git config user.email "andy@example.com"
git config user.name "Andyinjiner"
git add -A
git commit -m "Initial commit: ESP32 Growbox with Wokwi simulation"
git branch -M main
git push -u origin main
