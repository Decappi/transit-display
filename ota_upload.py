# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nikita Khakham

#!/usr/bin/env python3
"""
OTA uploader for ESP32 behind Docker NAT.
Fixes the issue where espota.py uses ephemeral UDP port for invitation,
and the ESP's UDP response gets lost on NAT.

Usage:
  python3 ota_upload.py <ESP_IP> <firmware.bin> [HOST_IP] [HOST_PORT]
"""
import socket, hashlib, os, sys, struct, time

ESP_PORT = 8266
HOST_PORT = int(os.environ.get('OTA_PORT', '36842'))
HOST_IP = os.environ.get('OTA_HOST', '192.168.0.188')

def main(esp_ip, firmware_path, host_ip=HOST_IP, host_port=HOST_PORT):
    content_size = os.path.getsize(firmware_path)
    with open(firmware_path, 'rb') as f:
        file_md5 = hashlib.md5(f.read()).hexdigest()

    print(f"ESP: {esp_ip}:{ESP_PORT}")
    print(f"Host: {host_ip}:{host_port}")
    print(f"Size: {content_size}  MD5: {file_md5}")

    # 1. Set up TCP listener on mapped port
    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp.bind(('0.0.0.0', host_port))
    tcp.listen(1)
    tcp.settimeout(15)
    print(f"TCP listening on 0.0.0.0:{host_port}")

    # 2. Send UDP invitation from a BOUND port so response reaches us
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.bind(('0.0.0.0', host_port))
    udp.settimeout(5)

    msg = f"0 {host_port} {content_size} {file_md5}\n"
    print(f"Sending invitation to {esp_ip}:{ESP_PORT} ...")
    for i in range(5):
        udp.sendto(msg.encode(), (esp_ip, ESP_PORT))
        try:
            data, addr = udp.recvfrom(1024)
            print(f"Response from {addr}: {data}")
            break
        except socket.timeout:
            print(f"  attempt {i+1}: no response")
            continue
    else:
        print("No response from ESP. Is OTA running?")
        udp.close()
        tcp.close()
        return 1

    # 3. Accept TCP connection from ESP
    print("Waiting for ESP to connect...")
    try:
        conn, addr = tcp.accept()
        print(f"ESP connected from {addr}")
    except socket.timeout:
        print("TCP timeout - ESP didn't connect")
        udp.close()
        tcp.close()
        return 1

    # 4. Send firmware
    sent_total = 0
    with open(firmware_path, 'rb') as f:
        while True:
            chunk = f.read(4096)
            if not chunk:
                break
            conn.sendall(chunk)
            sent_total += len(chunk)
            # Wait for ACK
            try:
                ack = conn.recv(4)
            except:
                pass
            pct = sent_total * 100 // content_size
            print(f"\rProgress: {pct}% ({sent_total}/{content_size})", end='', flush=True)

    print(f"\nSent {sent_total} bytes")
    conn.close()
    udp.close()
    tcp.close()
    print("OTA complete!")
    return 0

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 ota_upload.py <ESP_IP> <firmware.bin> [HOST_IP] [HOST_PORT]")
        sys.exit(1)
    host_ip = sys.argv[3] if len(sys.argv) > 3 else HOST_IP
    host_port = int(sys.argv[4]) if len(sys.argv) > 4 else HOST_PORT
    sys.exit(main(sys.argv[1], sys.argv[2], host_ip, host_port))
