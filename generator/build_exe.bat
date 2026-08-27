@echo off
setlocal

cd /d "%~dp0"

python -m pip install --upgrade pip
python -m pip install -r requirements.txt

pyinstaller --noconfirm --clean OSAL_Code_Generator.spec

echo.
echo Build completed. EXE is in dist\OSAL_Code_Generator.exe
pause
