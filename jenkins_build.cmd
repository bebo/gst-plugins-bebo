@ECHO ON
set errorlevel=
set FILENAME=%TEMP%\%JOB_NAME%_%ENV%_%TAG%.zip

rmdir /s /q dist
rmdir /s /q x64
rmdir /s /q Release

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

mkdir dist
if errorlevel 1 (
  echo "mkdir dist failed with %errorlevel%"
  exit /b %errorlevel%
)

xcopy x64\Release\*.DLL dist

if errorlevel 1 (
  echo "Failed xcopy x64\Release\*.DLL dist with %errorlevel%"
  exit /b %errorlevel%
)

xcopy x64\Release\*.pdb dist
if errorlevel 1 (
    echo "Failed xcopy x64\Release\*.pdb dist with %errorlevel%"
    exit /b %errorlevel%
)


"C:\Program Files\7-Zip\7z.exe" a -r %FILENAME% -w .\dist\* -mem=AES256

@if errorlevel 1 (
    echo "zip failed with %errorlevel%"
    exit /b %errorlevel%
)

if "%LIVE%" == "true" (
    "C:\Python34\python.exe" "C:\w\jenkins_uploader.py" --project %JOB_NAME% --tag %TAG% --env %ENV%
    @if errorlevel 1 (
      echo "jenkins_upload failed with %errorlevel%"
      exit /b %errorlevel%
    )
) else (
    "C:\Python34\python.exe" "C:\w\jenkins_uploader.py" --project %JOB_NAME% --tag %TAG% --env %ENV% --no-deploy
    @if errorlevel 1 (
      echo "jenkins_upload failed with %errorlevel%"
      exit /b %errorlevel%
    )
)
