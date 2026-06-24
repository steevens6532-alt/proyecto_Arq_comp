# ============================================
# Dockerfile - Bridge Arduino-WebSocket
# ============================================
FROM python:3.12-slim

WORKDIR /app

# Instala dependencias
RUN pip install --no-cache-dir pyserial websockets

# Copia el script
COPY bridge.py .

# Puerto del WebSocket
EXPOSE 8765

CMD ["python", "bridge.py"]
