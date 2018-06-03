@echo off

rem set compiler_flags=-Od -MT -nologo -fp:fast -fp:except- -Gm- -GR- -Zo -WX -W4 -wd4201 -wd4100 -wd4189 -wd4505 -wd4127 -FC -Z7
set compiler_flags=-Od -nologo -fp:fast -fp:except- -W4 -wd4505 -wd4201 -Z7
set linker_flags=-incremental:no -opt:ref -debug:full

IF NOT EXIST .\build mkdir .\build
IF NOT EXIST .\build\x86 mkdir .\build\x86

cl %compiler_flags% -I3party -Iinclude src\cmantic.cpp -link %linker_flags% SDL2.lib opengl32.lib  -debug
popd
