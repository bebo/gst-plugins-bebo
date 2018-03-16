ECHO ON
set errorlevel=

set FILENAME=%JOBNAME%_%TAG%.zip

rmdir /s /q dist
rmdir /s /q x64
rmdir /s /q Release
del /Q /F %FILENAME%

@REM build servers doesn't have this variable - we probably should be using vswhere
set VS2017INSTALLDIR=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional

pushd .
call "%VS2017INSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" amd64
popd

MSBuild.exe /property:Configuration=Release /property:Platform=x64 /target:clean,build

@if errorlevel 1 (
  exit /b %errorlevel%
)
@ECHO ON

mkdir dist
@if errorlevel 1 (
  exit /b %errorlevel%
)

xcopy x64\Release\*.DLL dist
@if errorlevel 1 (
  exit /b %errorlevel%
)

xcopy x64\Release\*.pdb dist

@if errorlevel 1 (
  exit /b %errorlevel%
)


"C:\Program Files\7-Zip\7z.exe" a -r %FILENAME% -w .\dist\* -mem=AES256

@if errorlevel 1 (
  exit /b %errorlevel%
)

"C:\Program Files\Amazon\AWSCLI\aws.exe" s3api put-object --bucket bebo-app --key repo/bebo-gst-to-dshow/%FILENAME% --body %FILENAME%

@if errorlevel 1 (
  exit /b %errorlevel%
)
