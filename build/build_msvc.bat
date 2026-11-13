@echo off
rem Builds BodyHook.asi (SLAM mod) with Visual Studio. Run from the "x86 Native Tools Command Prompt for VS".
cd /d "%~dp0.."
if not exist bin mkdir bin
cl /nologo /O2 /GS- /Gs999999 /Zl /W3 /Islam /DMOD_HAS_STOP /DMOD_NAME=\"BodyHook\" /DMOD_VERSION=\"2.2.1\" /LD src\bodyhook.c slam\slam_glue.c /Fobin\ /Fe:bin\BodyHook.asi /link /nodefaultlib /entry:DllMain /noimplib kernel32.lib user32.lib || exit /b 1
del /q bin\*.obj bin\*.exp 2>nul
echo Built bin\BodyHook.asi
