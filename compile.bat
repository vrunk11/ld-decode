@echo off
mkdir build
cd build
cmake -G "MinGW Makefiles" ../  -DCMAKE_BUILD_TYPE=Debug
make -j 3
move /y E:\Dev\git\ld-decode-sequence-check\ld-decode\build\tools\ld-sequence-check\ld-sequence-check.exe E:\Dev\git\ld-decode-tools-build-template\ld-sequence-check.exe
pause