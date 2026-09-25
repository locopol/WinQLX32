@ECHO OFF
SET WINQLX32_DIR=%~dp0

START /B /I " " "%WINQLX32_DIR%"\launcher.exe quakelive_steam.exe "%WINQLX32_DIR%winqlx.dll"
"%WINQLX32_DIR%"\quakelive_steam.exe ^
+set sv_vac 0 ^
+set dedicated 1 ^
+set net_port 27960 ^
+set sv_demoRecord 0 ^
+set qlx_logs 1 ^
+set qlx_logsSize 100024 ^
+set qlx_owner 76561198276850182 ^
+set qlx_plugins "plugin_manager, essentials, motd, permission, ban, silence, clan, names, log, workshop" ^
+set qlx_motdSound "sound/vo/crash_new/37b_07_alt.wav" ^
+set qlx_motdHeader "^6======= ^7MESSAGE OF THE DAY ^6=======\nWELCOME TO QUAKE LIVE ON WINDOWS RUNNING MINQLX" ^
+set qlx_serverBrandName "^2Win^3QLX^132 ^3BRANDED SERVER" ^
+set qlx_connectMessage "WinQLX32 is alive!!"

