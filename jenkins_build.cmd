@ECHO ON
set errorlevel=

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


@REM build servers doesn't have this variable - we probably should be using vswhere
set VS2017INSTALLDIR=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional

pushd .
call "%VS2017INSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" amd64
popd

MSBuild.exe /property:Configuration=Release /property:Platform=x64 /target:clean,build

if errorlevel 1 (
  echo "Build Failed with %errorlevel%"
  exit /b %errorlevel%
)
xcopy x64\Release\*.DLL .\build\gst\Release
xcopy x64\Release\*.pdb .\build\gst\Release


cd build
set FILENAME=gst-plugins-bebo_%TAG%.zip
"C:\Program Files\7-Zip\7z.exe" a -r ..\%FILENAME% -w .\gst\Release\* -mem=AES256

@if errorlevel 1 (
  exit /b %errorlevel%
)

cd ..
"C:\Program Files\Amazon\AWSCLI\aws.exe" s3api put-object --bucket bebo-app --key repo/gst-plugins-bebo/%FILENAME% --body %FILENAME%

@if errorlevel 1 (
  exit /b %errorlevel%
)

@echo "Uploaded artifact gst-plugins-bebo/%FILENAME%"

