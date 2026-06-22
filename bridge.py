# ============================================
# bridge.py — Arduino Serial <-> WebSocket
# pip install pyserial websockets
# ============================================

import asyncio
import serial
import websockets
import json
import os
from datetime import datetime

# ── Configuración ──────────────────────────
PUERTO  = os.environ.get("ARDUINO_PORT", "COM3")  # Linux: /dev/ttyACM0 o /dev/ttyUSB0  Windows: COM3
BAUDIOS = 9600
WS_PORT = 8765
# ───────────────────────────────────────────

clientes = set()       # clientes WebSocket conectados
arduino  = None        # referencia global a la conexión Serial

# Estado actual del sistema
estado = {
    "pir":       False,
    "led":       False,
    "buzzer":    False,
    "alerta":    None,
    "alerta_id": 0,     # se incrementa solo cuando llega una alerta NUEVA real
    "status":    "listo",
    "nivel":     None,    # lleno / medio / vacio
    "servo":     None,    # abierto / cerrado
    "hora":      None,
}

def parsear(linea: str) -> dict | None:
    """Convierte 'PIR:1' en {'tipo':'PIR', 'valor':'1'}"""
    linea = linea.strip()
    if ":" not in linea:
        return None
    tipo, valor = linea.split(":", 1)
    return {"tipo": tipo.upper(), "valor": valor}

def aplicar(msg: dict):
    """Actualiza el estado global con el mensaje recibido del Arduino"""
    t, v = msg["tipo"], msg["valor"]
    estado["hora"] = datetime.now().strftime("%H:%M:%S")

    if   t == "PIR":    estado["pir"]    = v == "1"
    elif t == "LED":    estado["led"]    = v == "1"
    elif t == "BUZZ":   estado["buzzer"] = v == "1"
    elif t == "STATUS": estado["status"] = v
    elif t == "NIVEL":
        estado["nivel"]  = v          # lleno / medio / vacio
        estado["alerta"] = None       # limpia la alerta vieja en cada lectura periódica
    elif t == "SERVO":
        estado["servo"]  = v          # cerrado
        estado["alerta"] = None       # limpia la alerta al cerrar el servo
    elif t == "ALERTA":
        estado["alerta"]    = v
        estado["alerta_id"] = estado.get("alerta_id", 0) + 1   # ID único por evento real
        if v == "dispensando":
            estado["servo"] = "abierto"   # se infiere que abrió justo antes de dispensar

contador_broadcast = 0

async def broadcast(payload: str):
    """Envía el estado a todos los clientes conectados"""
    global contador_broadcast
    contador_broadcast += 1
    print(f"  → broadcast #{contador_broadcast} a {len(clientes)} cliente(s)")
    if clientes:
        await asyncio.gather(*[c.send(payload) for c in clientes])

async def leer_serial():
    """Lee el puerto Serial y transmite cambios al dashboard"""
    global arduino
    print(f"[Serial] Conectando a {PUERTO} @ {BAUDIOS}...")
    try:
        arduino = serial.Serial(PUERTO, BAUDIOS, timeout=1)
        print(f"[Serial] Conectado ✓")
    except serial.SerialException as e:
        print(f"[Serial] Error: {e}")
        return

    loop = asyncio.get_event_loop()

    while True:
        linea = await loop.run_in_executor(None, arduino.readline)
        try:
            linea = linea.decode("utf-8").strip()
        except UnicodeDecodeError:
            continue

        if not linea:
            continue

        print(f"[Arduino {datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {linea}")
        msg = parsear(linea)
        if msg:
            aplicar(msg)
            await broadcast(json.dumps(estado))

async def manejar_cliente(ws):
    """Registra clientes y escucha comandos que mandan desde el dashboard"""
    clientes.add(ws)
    print(f"[WS] Cliente conectado. Total: {len(clientes)}")
    await ws.send(json.dumps(estado))

    try:
        async for mensaje in ws:
            # El dashboard manda algo como {"comando": "LED_ON"}
            try:
                data = json.loads(mensaje)
                comando = data.get("comando")
            except json.JSONDecodeError:
                continue

            if comando and arduino:
                print(f"[Dashboard → Arduino] {comando}")
                arduino.write((comando + "\n").encode("utf-8"))

    finally:
        clientes.discard(ws)
        print(f"[WS] Cliente desconectado. Total: {len(clientes)}")

async def main():
    print(f"[WS] Servidor en ws://localhost:{WS_PORT}")
    async with websockets.serve(manejar_cliente, "localhost", WS_PORT):
        await leer_serial()

if __name__ == "__main__":
    asyncio.run(main())
