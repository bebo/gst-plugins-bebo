@ECHO ON
set errorlevel=
set DIST_DIR=.\build\dist

mkdir build
cd build
cmake -G "Visual Studio 15 2017 Win64" ..

@if errorlevel 1 (
  exit /b %errorlevel%
)

cmake --build . --config Release

@if errorlevel 1 (
  exit /b %errorlevel%
)

cd ..


:: @jake - it looks like we're already using cmake --build to build, we shouldn't need to build it again with msbuild
::
:: @REM build servers doesn't have this variable - we probably should be using vswhere
:: set VS2017INSTALLDIR=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional
:: pushd .
:: call "%VS2017INSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" amd64
:: popd
:: MSBuild.exe /property:Configuration=Release /property:Platform=x64 /target:clean,build

if errorlevel 1 (
  echo "Build Failed with %errorlevel%"
  exit /b %errorlevel%
)

xcopy /Y .\build\gst\Release\*.dll %DIST_DIR%\gst-plugin\
xcopy /Y .\build\gst\Release\*.pdb %DIST_DIR%\gst-plugin\
xcopy /Y .\build\nacl-preview\Release\*.dll %DIST_DIR%\nacl-plugin\
xcopy /Y .\build\nacl-preview\Release\*.pdb %DIST_DIR%\nacl-plugin\

set FILENAME=gst-plugins-bebo_%TAG%.zip
"C:\Program Files\7-Zip\7z.exe" a -r %FILENAME% -w %DIST_DIR%\* -mem=AES256

@if errorlevel 1 (
  exit /b %errorlevel%
)

"C:\Program Files\Amazon\AWSCLI\aws.exe" s3api put-object --bucket bebo-app --key repo/gst-plugins-bebo/%FILENAME% --body %FILENAME%

@if errorlevel 1 (
  exit /b %errorlevel%
)

@echo "Uploaded artifact gst-plugins-bebo/%FILENAME%"

