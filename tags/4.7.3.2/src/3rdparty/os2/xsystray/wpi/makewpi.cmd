/**/

'REM // @echo off'

'mkdir tmp_wpi'
'mkdir tmp_wpi\plugin'
'copy ..\tmp\dist\plugin\*.dll tmp_wpi\plugin\'
'mkdir tmp_wpi\dll'
'copy ..\tmp\dist\apilib\*.dll tmp_wpi\dll\'
'mkdir tmp_wpi\apilib'
'copy ..\tmp\dist\apilib\*.lib tmp_wpi\apilib\'
'copy ..\apilib\xsystray.h tmp_wpi\apilib\'
'copy ..\README tmp_wpi\apilib\'
'copy ..\COPYING tmp_wpi\apilib\'

'cmd /c wic -a xsystray-0_1_1.wpi 1 -ctmp_wpi\plugin -r * 2 -ctmp_wpi\dll -r * 3 -ctmp_wpi\apilib -r * -s xsystray.wis'

'del tmp_wpi\apilib\* /n'
'rmdir tmp_wpi\apilib'
'del tmp_wpi\dll\* /n'
'rmdir tmp_wpi\dll'
'del tmp_wpi\plugin\* /n'
'rmdir tmp_wpi\plugin'
'rmdir tmp_wpi'
