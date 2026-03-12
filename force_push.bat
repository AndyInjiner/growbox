@echo off
cd /d "%~dp0"
git merge --abort 2>nul
git push -u origin main --force
