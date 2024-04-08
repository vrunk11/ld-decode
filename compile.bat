@echo off
mkdir build
cd build
cmake -G "MinGW Makefiles" ../  -DCMAKE_BUILD_TYPE=Debug
make -j 3
pause