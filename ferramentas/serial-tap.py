"""Le a serial da placa para um arquivo, com reset opcional.

O `idf.py monitor` e interativo e nao se deixa redirecionar bem; para conferir
um boot depois do fato basta o texto cru. Com --reset a placa e reiniciada
logo depois da porta abrir, entao a captura pega o boot desde a primeira linha.

    python ferramentas/serial-tap.py COM8 40 artifacts/boot.log --reset
"""
import sys
import time

import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
dest = sys.argv[3] if len(sys.argv) > 3 else "serial.log"
reset = "--reset" in sys.argv

with serial.Serial(port, 115200, timeout=0.2) as s, open(dest, "wb") as f:
    if reset:
        # No CH340 da E32R28T-1 o RTS chega no EN e o DTR no GPIO0. Com o
        # GPIO0 alto o pulso no EN e um boot normal, nao modo download.
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.12)
        s.setRTS(False)
    fim = time.time() + secs
    while time.time() < fim:
        data = s.read(4096)
        if data:
            f.write(data)
            f.flush()
print("fim")
