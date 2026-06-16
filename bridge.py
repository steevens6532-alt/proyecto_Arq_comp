# ============================================
# bridge.py — Arduino Serial → WebSocket
# pip install pyserial websockets
# ============================================

import asyncio
import serial
import websockets
import json
from datetime import datetime

# ── Configuración ──────────────────────────
PUERTO  = "COM3"       # Windows: COM3, COM4...  Mac/Linux: /dev/ttyUSB0
BAUDIOS = 9600
WS_PORT = 8765
# ───────────────────────────────────────────

clientes = set()       # clientes WebSocket conectados

# Estado actual del sistema
estado = {
    "pir":    False,
    "led":    False,
    "buzzer": False,
    "alerta": None,
    "status": "listo",
    "hora":   None,
}

def parsear(linea: str) -> dict | None:
    """Convierte 'PIR:1' en {'tipo':'PIR', 'valor':'1'}"""
    linea = linea.strip()
    if ":" not in linea:
        return None
    tipo, valor = linea.split(":", 1)
    return {"tipo": tipo.upper(), "valor": valor}

def aplicar(msg: dict):
    """Actualiza el estado global con el mensaje recibido"""
    t, v = msg["tipo"], msg["valor"]
    estado["hora"] = datetime.now().strftime("%H:%M:%S")

    if   t == "PIR":    estado["pir"]    = v == "1"
    elif t == "LED":    estado["led"]    = v == "1"
    elif t == "BUZZ":   estado["buzzer"] = v == "1"
    elif t == "ALERTA": estado["alerta"] = v
    elif t == "STATUS": estado["status"] = v

async def broadcast(payload: str):
    """Envía el estado a todos los clientes conectados"""
    if clientes:
        await asyncio.gather(*[c.send(payload) for c in clientes])

async def leer_serial():
    """Lee el puerto Serial y transmite cambios al dashboard"""
    print(f"[Serial] Conectando a {PUERTO} @ {BAUDIOS}...")
    try:
        arduino = serial.Serial(PUERTO, BAUDIOS, timeout=1)
        print(f"[Serial] Conectado ✓")
    except serial.SerialException as e:
        print(f"[Serial] Error: {e}")
        return

    loop = asyncio.get_event_loop()

    while True:
        # Leer sin bloquear el event loop de asyncio
        linea = await loop.run_in_executor(None, arduino.readline)
        try:
            linea = linea.decode("utf-8").strip()
        except UnicodeDecodeError:
            continue

        if not linea:
            continue

        print(f"[Arduino] {linea}")
        msg = parsear(linea)
        if msg:
            aplicar(msg)
            await broadcast(json.dumps(estado))

async def manejar_cliente(ws):
    """Registra y desregistra clientes WebSocket"""
    clientes.add(ws)
    print(f"[WS] Cliente conectado. Total: {len(clientes)}")
    # Enviar estado actual al nuevo cliente
    await ws.send(json.dumps(estado))
    try:
        await ws.wait_closed()
    finally:
        clientes.discard(ws)
        print(f"[WS] Cliente desconectado. Total: {len(clientes)}")

async def main():
    print(f"[WS] Servidor en ws://localhost:{WS_PORT}")
    async with websockets.serve(manejar_cliente, "localhost", WS_PORT):
        await leer_serial()

if __name__ == "__main__":
    asyncio.run(main())
