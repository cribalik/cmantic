@echo off

rem set compiler_flags=-Od -MT -nologo -fp:fast -fp:except- -Gm- -GR- -Zo -WX -W4 -wd4201 -wd4100 -wd4189 -wd4505 -wd4127 -FC -Z7
set compiler_flags=-Od -nologo -fp:fast -fp:except- -W4 -wd4505 -wd4201 -wd4408 -Z7
set linker_flags=-incremental:no -opt:ref -debug:full -profile

IF NOT EXIST .\build mkdir .\build
IF NOT EXIST .\build\x86 mkdir .\build\x86

cl %compiler_flags% -I3party -Iinclude tools\coroutines.cpp -link %linker_flags% SDL2.lib opengl32.lib -debug
coroutines.exe src\cmantic.cpp src\out.cpp

cl %compiler_flags% -I3party -Iinclude .\src\out.cpp -Fecmantic.exe -link %linker_flags% SDL2.lib opengl32.lib -debug

del src\out.cpp

popd
