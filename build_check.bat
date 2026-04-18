@echo off
call "D:\2.SoftwareDev\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

cd /d "D:\0.PC\Desktop\CurrencyWar_Cpp"

echo ============================================
echo Compiling ocr_engine.cpp (syntax check)
echo ============================================
cl /c /std:c++17 /EHsc /utf-8 /W3 ^
    /I"src" ^
    /I"third_party\paddle_inference" ^
    /I"third_party\opencv_stub" ^
    src\core\ocr_engine.cpp ^
    /Fo"build_test\ocr_engine.obj" 2>&1

echo.
echo Exit code: %ERRORLEVEL%
