@echo offE

set CURRENT_DIR = %~dp0
premake\premake5 %1

msbuild build /p:configuration=%2 /p:platform=x64