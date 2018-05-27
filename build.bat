@echo off

set compiler_flags=-Od -MT -nologo -fp:fast -fp:except- -Gm- -GR- -Zo -Oi -WX -W4 -wd4201 -wd4100 -wd4189 -wd4505 -wd4127 -FC -Z7
set linker_flags=-incremental:no -opt:ref

IF NOT EXIST .\build mkdir .\build
IF NOT EXIST .\build\x86 mkdir .\build\x86

pushd .\build\x86
dir ..\..\
cl %compiler_flags% -I..\..\3party -I..\..\include ..\..\src\cmantic.cpp -link %linker_flags% ..\..\SDL2.lib opengl32.lib  -debug
popd
