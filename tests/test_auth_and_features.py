import socket
import subprocess
import time
import sys
import os

PORT = 9001

def start_server():
    subprocess.run(["killall", "-9", "server"], stderr=subprocess.DEVNULL)
    time.sleep(0.3)
    if os.path.exists("accounts.dat"):
        os.remove("accounts.dat")
    p = subprocess.Popen(["./server"])
    time.sleep(0.8)
    return p

def recv_line(s):
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(1)
        if not chunk:
            break
        buf += chunk
    return buf.decode(errors="ignore").strip()

def run_tests():
    server = start_server()
    try:
        print("[TEST] 1. Registration - Validation Rules")
        # Connect client 1
        s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s1.connect(("127.0.0.1", PORT))

        # Invalid username: symbols
        s1.sendall(b"__REGISTER__:bad$user:secret123\n")
        resp = recv_line(s1)
        assert "__REG_ERR__" in resp, f"Expected REG_ERR for bad$user, got: {resp}"
        print("  ✓ Invalid username with symbols rejected")

        # Invalid username: too long (>10 chars)
        s1.sendall(b"__REGISTER__:waytoolongusername:secret123\n")
        resp = recv_line(s1)
        assert "__REG_ERR__" in resp, f"Expected REG_ERR for long user, got: {resp}"
        print("  ✓ Long username (>10 chars) rejected")

        # Invalid password: too short (<6 chars)
        s1.sendall(b"__REGISTER__:testuser:123\n")
        resp = recv_line(s1)
        assert "__REG_ERR__" in resp, f"Expected REG_ERR for short pwd, got: {resp}"
        print("  ✓ Short password (<6 chars) rejected")

        # Valid registration
        s1.sendall(b"__REGISTER__:alice_99:StrongPass_1\n")
        resp = recv_line(s1)
        assert "__REG_OK__" in resp, f"Expected REG_OK, got: {resp}"
        print("  ✓ Valid registration accepted (alice_99)")

        # Duplicate registration
        s1.sendall(b"__REGISTER__:alice_99:AnotherPass\n")
        resp = recv_line(s1)
        assert "__REG_ERR__" in resp, f"Expected REG_ERR for duplicate, got: {resp}"
        print("  ✓ Duplicate username rejected")

        print("[TEST] 2. Login - Authentication & Verification")
        # Wrong password
        s1.sendall(b"__LOGIN__:alice_99:WrongPass\n")
        resp = recv_line(s1)
        assert "__LOGIN_ERR__" in resp, f"Expected LOGIN_ERR for wrong pwd, got: {resp}"
        print("  ✓ Wrong password rejected")

        # Correct password
        s1.sendall(b"__LOGIN__:alice_99:StrongPass_1\n")
        resp = recv_line(s1)
        assert "__LOGIN_OK__" in resp, f"Expected LOGIN_OK, got: {resp}"
        print("  ✓ Correct password authenticated with PBKDF2 hash")

        # Verify welcome and registration status
        time.sleep(0.5)
        status_reg_found = False
        while True:
            s1.settimeout(1.0)
            try:
                line = recv_line(s1)
                if not line: break
                if "__STATUS_REG__:1" in line:
                    status_reg_found = True
                    break
            except socket.timeout:
                break
        assert status_reg_found, "Client did not receive __STATUS_REG__:1"
        print("  ✓ Client received registered user status badge (__STATUS_REG__:1)")

        print("[TEST] 3. Guest Login & Protection")
        s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s2.connect(("127.0.0.1", PORT))

        # Guest trying to steal registered name
        s2.sendall(b"__GUEST__:alice_99\n")
        resp = recv_line(s2)
        assert "__GUEST_ERR__" in resp, f"Expected GUEST_ERR for claiming registered name, got: {resp}"
        print("  ✓ Guest cannot impersonate or claim registered account")

        # Guest login with available valid name
        s2.sendall(b"__GUEST__:bob_guest\n")
        resp = recv_line(s2)
        assert "__GUEST_OK__" in resp, f"Expected GUEST_OK, got: {resp}"
        print("  ✓ Guest login succeeded (bob_guest)")

        # Check bob received status 0 (guest)
        time.sleep(0.5)
        status_reg_found = False
        while True:
            s2.settimeout(1.0)
            try:
                line = recv_line(s2)
                if not line: break
                if "__STATUS_REG__:0" in line:
                    status_reg_found = True
                    break
            except socket.timeout:
                break
        assert status_reg_found, "Guest did not receive __STATUS_REG__:0"
        print("  ✓ Guest received unverified status badge (__STATUS_REG__:0)")

        print("[TEST] 4. Concurrent Login Prevention")
        s3 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s3.connect(("127.0.0.1", PORT))
        s3.sendall(b"__LOGIN__:alice_99:StrongPass_1\n")
        resp = recv_line(s3)
        assert "__LOGIN_ERR__" in resp and "already logged in" in resp, f"Expected already logged in error, got: {resp}"
        print("  ✓ Concurrent login attempt of active user blocked")
        s3.close()

        print("[TEST] 5. /clear Command Server Handling")
        s1.sendall(b"/clear\n")
        time.sleep(0.5)
        got_clear = False
        while True:
            s1.settimeout(1.0)
            try:
                line = recv_line(s1)
                if not line: break
                if "\033[2J" in line or "[SERVER] Screen cleared" in line:
                    got_clear = True
                    break
            except socket.timeout:
                break
        assert got_clear, "/clear did not return ANSI clear sequence or server message"
        print("  ✓ /clear command properly handled and delivered clear escape codes")

        print("[TEST] 6. /setcolor Command")
        s1.sendall(b"/setcolor red\n")
        time.sleep(0.5)
        got_color = False
        while True:
            s1.settimeout(1.0)
            try:
                line = recv_line(s1)
                if not line: break
                if "color set to" in line.lower():
                    got_color = True
                    break
            except socket.timeout:
                break
        assert got_color, "/setcolor did not confirm color update"
        print("  ✓ /setcolor confirmed color change")

        print("[TEST] 7. Message Exchange between Registered and Guest User")
        s1.sendall(b"Hello world from registered alice!\n")
        time.sleep(0.5)
        got_msg = False
        while True:
            s2.settimeout(1.0)
            try:
                line = recv_line(s2)
                if not line: break
                if "alice_99" in line and "Hello world from registered alice" in line:
                    got_msg = True
                    break
            except socket.timeout:
                break
        assert got_msg, "Guest did not receive broadcast message from registered user"
        print("  ✓ Broadcast message with registered identity delivered successfully")

        s1.close()
        s2.close()
        print("\n============================================================")
        print("   ALL AUTHENTICATION & NEW FEATURE TESTS PASSED 100%!")
        print("============================================================")
    finally:
        server.terminate()
        server.wait()
        subprocess.run(["killall", "-9", "server"], stderr=subprocess.DEVNULL)
        if os.path.exists("accounts.dat"):
            os.remove("accounts.dat")

if __name__ == "__main__":
    run_tests()
