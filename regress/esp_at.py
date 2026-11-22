#!/usr/bin/env python3
"""Test TCP and UDP through the simulated UART. Requires Python 3 and Thumbulator."""

import errno
from pathlib import Path
from queue import Queue
import socket
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread


def uart_proxy_rom():
    # Cortex-M0 ROM forwarding console RX to ESP TX and ESP RX to console TX.
    # The branch back to the loop is at 0x24. Constants follow at 0x28.
    vectors = (0x40080000, 0x00000009)
    code = (
        0x4C07,              # ldr r4, [pc, #28]    ; ESP UART
        0x4D08,              # ldr r5, [pc, #32]    ; console UART
        0x68A0,              # loop: ldr r0, [r4, #8]
        0x07C1,              # lsls r1, r0, #31     ; RX ready in N
        0xD501,              # bpl tx
        0x6861,              # ldr r1, [r4, #4]
        0x6029,              # str r1, [r5]
        0x0780,              # tx: lsls r0, r0, #30 ; TX ready in N
        0xD5F8,              # bpl loop
        0x68A8,              # ldr r0, [r5, #8]
        0x2800,              # cmp r0, #0
        0xD0F5,              # beq loop
        0x6868,              # ldr r0, [r5, #4]
        0x6020,              # str r0, [r4]
        0xE7F2,              # b loop
        0xBF00,              # align the literal pool
    )
    return struct.pack("<II16HII", *vectors, *code, 0xE0000040, 0xE0000000)


class Modem:
    def __init__(self, process):
        self.process = process
        self.rx = Queue()
        Thread(target=self.read_stdout, daemon=True).start()

    def read_stdout(self):
        while True:
            byte = self.process.stdout.read(1)
            self.rx.put(byte)
            if not byte:
                return

    def send(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def expect(self, suffix):
        data = bytearray()
        while not data.endswith(suffix):
            byte = self.rx.get(timeout=10)
            if not byte:
                raise AssertionError(f"Thumbulator exited while waiting for {suffix!r}: {data!r}")
            data += byte
        return bytes(data)

    def command(self, command, reply=b"\r\nOK\r\n"):
        self.send(command.encode("ascii") + b"\r\n")
        return self.expect(reply)

    def send_payload(self, command, payload, peer):
        assert self.command(command, b"> ").endswith(b"OK\r\n> ")
        self.send(payload)
        result = bytearray()
        while len(result) < len(payload):
            data = peer.recv(len(payload) - len(result))
            assert data
            result.extend(data)
        assert result == payload
        self.expect(b"SEND OK\r\n")


def exercise(modem):
    assert modem.command("AT") == b"AT\r\n\r\nOK\r\n"
    modem.command("ATE0")
    assert modem.command("AT") == b"\r\nOK\r\n"
    modem.command('AT+CIPSTART=9,"TCP","localhost",8', b"\r\nERROR\r\n")

    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(10)
        port = listener.getsockname()[1]
        assert b"0,CONNECT" in modem.command(f'AT+CIPSTART=0,"TCP","localhost",{port}')
        peer, _ = listener.accept()
        with peer:
            peer.settimeout(10)
            modem.send_payload("AT+CIPSEND=0,3", b"A\0B", peer)
            peer.sendall(b"ok")
            modem.expect(b"+IPD,0,2:ok")
        modem.expect(b"0,CLOSED\r\n")

    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    modem.command(f"AT+CIPSERVER=1,{port}")
    with socket.create_connection(("127.0.0.1", port), timeout=10) as peer:
        modem.expect(b"0,CONNECT\r\n")
        peer.sendall(b"ok")
        modem.expect(b"+IPD,0,2:ok")
        modem.send_payload("AT+CIPSEND=0,3", b"B\0A", peer)
        assert b"0,CLOSED" in modem.command("AT+CIPSERVER=0,1")
        assert peer.recv(1) == b""

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(("127.0.0.1", 0))
        peer.settimeout(10)
        port = peer.getsockname()[1]
        modem.command("AT+CIPDINFO=1")
        assert b"0,CONNECT" in modem.command(f'AT+CIPSTART=0,"UDP","127.0.0.1",{port},0,2')
        modem.command(f'AT+CIPSEND=0,4,"127.0.0.1",{port}', b"> ")
        modem.send(b"A\0B\xff")
        payload, sender = peer.recvfrom(4096)
        assert payload == b"A\0B\xff"
        modem.expect(b"SEND OK\r\n")
        peer.sendto(b"\xff\0", sender)
        modem.expect(f'+IPD,0,2,"127.0.0.1",{port}:'.encode("ascii") + b"\xff\0")
        peer.sendto(b"", sender)
        modem.expect(f'+IPD,0,0,"127.0.0.1",{port}:'.encode("ascii"))
        assert b"0,CLOSED" in modem.command("AT+CIPCLOSE=0")

    modem.command("AT+RST", b"ready\r\n")
    assert modem.command("AT") == b"AT\r\n\r\nOK\r\n"


def start_simulator(binary, rom):
    pipes = dict(stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
    try:
        return subprocess.Popen([binary, rom], **pipes)
    except OSError as error:
        if error.errno != errno.ENOEXEC:
            raise
        # APE executables can use the shell fallback on hosts without binfmt.
        return subprocess.Popen(["sh", "-c", 'exec "$@"', "sh", binary, rom], **pipes)


if __name__ == "__main__":
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else "thumbulator").resolve()
    with TemporaryDirectory() as temp:
        rom = Path(temp) / "esp-at.bin"
        rom.write_bytes(uart_proxy_rom())
        with start_simulator(binary, rom) as process:
            try:
                exercise(Modem(process))
            finally:
                process.terminate()
    print("ESP-AT UART: TCP client, listener, and IPv4 UDP passed")
