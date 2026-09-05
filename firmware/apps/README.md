# Apps da placa

Cada pasta é um projeto ESP-IDF independente + `app.json`. O OS não
compila estes apps. Ver [`docs/ESP_APP_SDK.md`](../../docs/ESP_APP_SDK.md).

| Pasta | ID | Papel |
|-------|----|--------|
| `Sobre` | `com.ribanense.esp.sobre` | Versão, MAC, voltar ao OS |
| `Redes` | `com.ribanense.esp.redes` | Menu de rede; Scanner IP (ARP → `tmp/redes/hosts.bin`) |
