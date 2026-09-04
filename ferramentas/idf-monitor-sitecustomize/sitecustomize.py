# Ctrl+C encerra o idf_monitor. Sem isto o IDF captura SIGINT e manda 0x03 a placa.
import os
import signal


def _exit_monitor(_signum, _frame):
    os._exit(0)


try:
    signal.signal(signal.SIGINT, _exit_monitor)
except (ValueError, OSError):
    pass
