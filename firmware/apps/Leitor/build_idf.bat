@echo off
chcp 65001 >nul 2>&1
set IDF_PATH=C:\esp\esp-idf
set IDF_TOOLS_PATH=C:\esp\tools
set IDF_PYTHON_ENV_PATH=C:\esp\tools\python_env\idf5.3_py3.14_env
set ESP_IDF_VERSION=5.3.1
set PATH=%IDF_PYTHON_ENV_PATH%\Scripts;C:\esp\tools\tools\cmake\3.24.0\bin;C:\esp\tools\tools\ninja\1.11.1;C:\esp\tools\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin;C:\esp\tools\tools\idf-exe\1.0.3;C:\esp\tools\tools\ccache\4.10.2\ccache-4.10.2-windows-x86_64;%PATH%
cd /d "%~dp0"
python -X utf8 "%IDF_PATH%\tools\idf.py" %*
