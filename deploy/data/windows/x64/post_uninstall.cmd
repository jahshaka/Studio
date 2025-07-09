set JahshakaPath=%~dp0
echo %JahshakaPath%

"%JahshakaPath%\Jahshaka.exe" -c
timeout /t 1

taskkill /IM "Jahshaka.exe" /F
exit /b 0
