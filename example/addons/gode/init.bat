@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

if "%~1"=="" (
    set "PROJECT_DIR=%CD%"
) else (
    set "PROJECT_DIR=%~1"
)

echo Initializing gode project in: %PROJECT_DIR%

set "ADDON_SRC=%SCRIPT_DIR%"
set "ADDON_DST=%PROJECT_DIR%\addons\gode"

if /i "%ADDON_SRC%"=="%ADDON_DST%" (
    echo Already running inside target project, skipping addon copy
    goto :skip_copy
)

set "BUILD_CONFIG="
if exist "%ADDON_SRC%\bin\Release\libgode.dll" set "BUILD_CONFIG=Release"
if exist "%ADDON_SRC%\bin\Debug\libgode.dll"   set "BUILD_CONFIG=Debug"

if "%BUILD_CONFIG%"=="" (
    echo ERROR: No built binaries found in %ADDON_SRC%\bin\
    exit /b 1
)

mkdir "%ADDON_DST%\bin\%BUILD_CONFIG%" 2>nul
mkdir "%ADDON_DST%\core"              2>nul
mkdir "%ADDON_DST%\script"            2>nul

xcopy /y /q "%ADDON_SRC%\bin\%BUILD_CONFIG%\*" "%ADDON_DST%\bin\%BUILD_CONFIG%\" >nul
copy  /y    "%ADDON_SRC%\plugin.cfg"            "%ADDON_DST%\"                    >nul
copy  /y    "%ADDON_SRC%\gode.gd"               "%ADDON_DST%\"                    >nul
if exist "%ADDON_SRC%\gode.gd.uid" copy /y "%ADDON_SRC%\gode.gd.uid" "%ADDON_DST%\" >nul
copy  /y    "%ADDON_SRC%\init.bat"              "%ADDON_DST%\"                    >nul

xcopy /y /q "%ADDON_SRC%\script\*.gd"    "%ADDON_DST%\script\" >nul
xcopy /y /q "%ADDON_SRC%\script\*.uid"   "%ADDON_DST%\script\" >nul 2>nul
xcopy /y /q "%ADDON_SRC%\core\*.d.ts"    "%ADDON_DST%\core\"   >nul 2>nul

echo Copied addons\gode (%BUILD_CONFIG%)

:skip_copy

set "TSCONFIG=%PROJECT_DIR%\tsconfig.json"
if exist "%TSCONFIG%" (
    echo tsconfig.json already exists, skipping
) else (
    (
        echo {
        echo   "compilerOptions": {
        echo     "target": "ES2020",
        echo     "module": "ESNext",
        echo     "moduleResolution": "bundler",
        echo     "strict": false,
        echo     "noCheck": false,
        echo     "outDir": "dist",
        echo     "rootDir": ".",
        echo     "esModuleInterop": true,
        echo     "skipLibCheck": true,
        echo     "jsx": "react",
        echo   },
        echo   "include": ["**/*.ts", "**/*.tsx"],
        echo   "exclude": ["node_modules", "dist"]
        echo }
    ) > "%TSCONFIG%"
    echo Created tsconfig.json
)

set "PKGJSON=%PROJECT_DIR%\package.json"
if exist "%PKGJSON%" (
    echo package.json already exists, skipping
) else (
    for %%I in ("%PROJECT_DIR%") do set "PKG_NAME=%%~nxI"
    (
        echo {
        echo   "name": "!PKG_NAME!",
        echo   "version": "1.0.0",
        echo   "type": "module",
        echo   "scripts": {
        echo     "build": "tsc ^&^& tsc-alias",
        echo     "watch": "tsc --watch ^& tsc-alias --watch"
        echo   },
        echo   "dependencies": {
        echo     "typescript": "^5.9.3"
        echo   }
        echo }
    ) > "%PKGJSON%"
    echo Created package.json
)

echo Installing npm dependencies...
cd /d "%PROJECT_DIR%"
call npm install
echo npm install done

set "GITIGNORE=%PROJECT_DIR%\.gitignore"
findstr /c:"node_modules" "%GITIGNORE%" >nul 2>nul || (
    echo. >> "%GITIGNORE%"
    echo node_modules/ >> "%GITIGNORE%"
    echo dist/ >> "%GITIGNORE%"
    echo Updated .gitignore
)

echo.
echo Done! Project initialized.
echo   Build scripts: npm run build / npm run watch
echo   Enable the gode plugin in: Project ^> Project Settings ^> Plugins

endlocal
