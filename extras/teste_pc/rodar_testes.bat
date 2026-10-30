@echo off
rem Testes da CorFacil no PC: nao precisa de placa nem de sensor.
rem   1. teste_classificador: bordas da linha, marcador verde, faixa vermelha, laranja, robo passando
rem   2. sil\teste_biblioteca: a biblioteca e os exemplos rodando com um TCS34725 simulado,
rem      como ESP32 (memoria NVS) e como Arduino Uno (EEPROM)
rem Precisa de: python -m pip install ziglang   (a primeira compilacao demora ~1 minuto)
setlocal
cd /d "%~dp0"
set FALHOU=0
echo Compilando...
python -m ziglang c++ -std=gnu++17 -O2 -Wall -Wextra -target x86_64-windows-gnu teste_classificador.cpp -o "%TEMP%\cf_classificador.exe" || (set FALHOU=1& goto fim)
"%TEMP%\cf_classificador.exe" > "%TEMP%\cf_classificador.txt"
if errorlevel 1 (set FALHOU=1& echo [FALHA] classificador& findstr /C:"[FALHA]" "%TEMP%\cf_classificador.txt") else (echo [OK]    classificador)
call :placa ESP32 "-DARDUINO_ARCH_ESP32 -DESP32"
call :placa AVR "-DARDUINO_ARCH_AVR"
:fim
echo.
echo Saida completa de cada teste: %TEMP%\cf_*.txt
if %FALHOU%==1 (echo ALGUM TESTE FALHOU) else (echo TODOS OS TESTES PASSARAM)
if not "%1"=="sem_pausa" pause
exit /b %FALHOU%

:placa
python -m ziglang c++ -std=gnu++17 -O1 -Wall -Wextra -Wno-unused-parameter %~2 -Isil -I..\..\src -target x86_64-windows-gnu sil\teste_biblioteca.cpp -o "%TEMP%\cf_%1.exe" || (set FALHOU=1& exit /b 1)
set MEM=%TEMP%\cf_memoria_%1.txt
set MEM2=%TEMP%\cf_memoria2_%1.txt
if exist "%MEM%" del "%MEM%"
if exist "%MEM2%" del "%MEM2%"
for %%c in (calibrar religar apagar depois_apagar botao clone sem_sensor rapido) do call :cenario %1 %%c "%MEM%"
for %%c in (dois dois_religar) do call :cenario %1 %%c "%MEM2%"
exit /b 0

:cenario
"%TEMP%\cf_%1.exe" %2 %3 > "%TEMP%\cf_%1_%2.txt"
if errorlevel 1 (set FALHOU=1& echo [FALHA] %1 %2& findstr /C:"[FALHA]" "%TEMP%\cf_%1_%2.txt") else (echo [OK]    %1 %2)
exit /b 0
