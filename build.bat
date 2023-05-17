set CURRENT_DIR = %0\..
echo %0

"%CURRENT_DIR%\premake\premake5.exe %1"