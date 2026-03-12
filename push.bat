@echo off
cd /d "%~dp0"
git add push.bat
git commit -m "Stage changes"
git rm --cached commit.bat force_push.bat push.bat
git commit -m "Remove temp bat files from repo"
git push
