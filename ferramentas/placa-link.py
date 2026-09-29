"""Fala com o canal RB da UART sem pulsar DTR/RTS.

Nesta placa o RTS e o EN e o DTR e o GPIO0. Abrir a porta com os dois
soltos evita reset e modo de download.

  python ferramentas/placa-link.py COM8 tela artifacts/tela.bmp
  python ferramentas/placa-link.py COM8 toque 120 160
  python ferramentas/placa-link.py COM8 arrasto 40 40 200 280
"""
import sys
import time

import serial


class Rx:
    def __init__(self, ser):
        self.ser = ser
        self.buf = b""

    def _pull(self, deadline):
        chunk = self.ser.read(max(1, self.ser.in_waiting))
        if chunk:
            self.buf += chunk
            return True
        return time.time() < deadline

    def wait(self, token, timeout):
        deadline = time.time() + timeout
        while True:
            i = self.buf.find(token)
            if i >= 0:
                self.buf = self.buf[i:]
                return
            if time.time() >= deadline or not self._pull(deadline):
                raise TimeoutError("sem resposta %s" % token.decode("ascii", "replace"))

    def line(self, timeout):
        deadline = time.time() + timeout
        while True:
            i = self.buf.find(b"\n")
            if i >= 0:
                raw = self.buf[:i].replace(b"\r", b"")
                self.buf = self.buf[i + 1 :]
                return raw.decode("ascii", "replace")
            if time.time() >= deadline or not self._pull(deadline):
                raise TimeoutError("linha incompleta")

    def exact(self, n, timeout):
        deadline = time.time() + timeout
        while len(self.buf) < n:
            if time.time() >= deadline or not self._pull(deadline):
                raise TimeoutError("faltam %d bytes" % (n - len(self.buf)))
        out = self.buf[:n]
        self.buf = self.buf[n:]
        return out


def open_port(name):
    ser = serial.Serial()
    ser.port = name
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.write_timeout = 3
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def rgb565_le(pix):
    c = pix[0] | (pix[1] << 8)
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return bytes((b, g, r))


def write_bmp(path, w, h, rgb):
    row = w * 3
    pad = (4 - (row % 4)) % 4
    stride = row + pad
    pixels = stride * h
    header = bytearray(54)
    header[0:2] = b"BM"
    size = 54 + pixels
    header[2:6] = size.to_bytes(4, "little")
    header[10:14] = (54).to_bytes(4, "little")
    header[14:18] = (40).to_bytes(4, "little")
    header[18:22] = w.to_bytes(4, "little", signed=True)
    header[22:26] = h.to_bytes(4, "little", signed=True)
    header[26:28] = (1).to_bytes(2, "little")
    header[28:30] = (24).to_bytes(2, "little")
    header[34:38] = pixels.to_bytes(4, "little")
    body = bytearray(pixels)
    for y in range(h):
        src = y * row
        dst = (h - 1 - y) * stride
        body[dst : dst + row] = rgb[src : src + row]
    with open(path, "wb") as f:
        f.write(header)
        f.write(body)


def cmd_tela(ser, path):
    rx = Rx(ser)
    ser.reset_input_buffer()
    ser.write(b"RB TELA\n")
    rx.wait(b"RBSHOT ", 8)
    header = rx.line(2)
    parts = header.split()
    if len(parts) != 3 or parts[0] != "RBSHOT":
        raise RuntimeError("cabecalho RBSHOT invalido: %s" % header)
    w, h = int(parts[1]), int(parts[2])
    if w <= 0 or h <= 0 or w > 480 or h > 480:
        raise RuntimeError("tamanho invalido %dx%d" % (w, h))
    rgb = bytearray(w * h * 3)
    deadline = time.time() + 25
    while True:
        if time.time() > deadline:
            raise TimeoutError("captura incompleta")
        line = rx.line(25)
        if line.startswith("RBEND"):
            break
        if not line.startswith("RBRECT "):
            continue
        nums = line.split()
        if len(nums) != 5:
            raise RuntimeError("retangulo invalido: %s" % line)
        x, y, rw, rh = (int(nums[1]), int(nums[2]), int(nums[3]), int(nums[4]))
        raw = rx.exact(rw * rh * 2, 20)
        for row in range(rh):
            for col in range(rw):
                px = x + col
                py = y + row
                if px < 0 or py < 0 or px >= w or py >= h:
                    continue
                i = (row * rw + col) * 2
                o = (py * w + px) * 3
                rgb[o : o + 3] = rgb565_le(raw[i : i + 2])
    write_bmp(path, w, h, rgb)
    print(path)


def cmd_gesto(ser, line, wait):
    rx = Rx(ser)
    ser.reset_input_buffer()
    ser.write(line)
    deadline = time.time() + 3
    while time.time() < deadline:
        if b"RBOCUPADO" in rx.buf:
            raise RuntimeError("placa ocupada")
        if b"RBOK" in rx.buf:
            time.sleep(wait)
            return
        rx._pull(deadline)
    raise TimeoutError("sem RBOK")


def main():
    if len(sys.argv) < 3:
        print("uso: placa-link.py COMx tela|toque|arrasto ...", file=sys.stderr)
        return 2
    port = sys.argv[1]
    action = sys.argv[2]
    with open_port(port) as ser:
        if action == "tela":
            path = sys.argv[3] if len(sys.argv) > 3 else "tela.bmp"
            cmd_tela(ser, path)
            return 0
        if action == "toque":
            if len(sys.argv) != 5:
                print("uso: placa-link.py COMx toque X Y", file=sys.stderr)
                return 2
            cmd_gesto(ser, ("RB TOQUE %s %s\n" % (sys.argv[3], sys.argv[4])).encode(), 0.08)
            print("ok")
            return 0
        if action == "arrasto":
            if len(sys.argv) != 7:
                print("uso: placa-link.py COMx arrasto X1 Y1 X2 Y2", file=sys.stderr)
                return 2
            cmd_gesto(
                ser,
                ("RB ARRASTO %s %s %s %s\n" % tuple(sys.argv[3:7])).encode(),
                0.2,
            )
            print("ok")
            return 0
    print("acao desconhecida: %s" % action, file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
