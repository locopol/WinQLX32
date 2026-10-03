@ECHO OFF

SET HOMEPATH=D:\quakelive\config
SET /a GAMEPORT=%1 + 27960
SET /a RCONPORT=%1 + 28960

%~dp0\launcher.exe ^
+set sv_vac 1 ^
+set qlx_logs 1 ^
+set sv_demoRecord 0 ^
+set net_port %GAMEPORT% ^
+set fs_homepath "%HOMEPATH%\%GAMEPORT%" ^
REM +set zmq_rcon_port %RCONPORT% ^
REM +set zmq_stats_port %GAMEPORT%

