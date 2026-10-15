import socket
import time
import subprocess
import os

def connect(name):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 9001))
    s.sendall(f"{name}\n".encode())
    time.sleep(0.1)
    return s

def recv_until(s, target, timeout=3.0):
    s.settimeout(timeout)
    buf = ""
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = s.recv(4096).decode('utf-8', errors='ignore')
            if not chunk:
                break
            buf += chunk
            if target in buf:
                return True, buf
        except socket.timeout:
            break
    return False, buf

def drain(s, timeout=0.2):
    s.settimeout(timeout)
    buf = ""
    while True:
        try:
            chunk = s.recv(4096).decode('utf-8', errors='ignore')
            if not chunk: break
            buf += chunk
        except socket.timeout:
            break
    return buf

def send_cmd(s, cmd, delay=0.7):
    if isinstance(cmd, str):
        cmd = cmd.encode()
    s.sendall(cmd)
    time.sleep(delay)

def main():
    subprocess.run(["killall", "-9", "server"], stderr=subprocess.DEVNULL)
    if os.path.exists("server.log"):
        os.remove("server.log")
    time.sleep(0.3)
    print("[TEST] Launching server...")
    srv = subprocess.Popen(["./server"])
    time.sleep(0.6)

    try:
        # 1. Connect user1 (alice)
        alice = connect("alice")
        drain(alice, 0.5)

        # 2. Connect user2 (bob)
        bob = connect("bob")
        drain(bob, 0.5)

        # Connect admin (root_admin)
        admin = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        admin.connect(('127.0.0.1', 9001))
        admin.sendall(b"admin\n")
        time.sleep(0.1)
        ok, res = recv_until(admin, "ask", 2.0)
        assert ok, f"Admin ask failed: {res}"
        admin.sendall(b"admin123\n")
        ok, res = recv_until(admin, "true", 2.0)
        assert ok, f"Admin auth failed: {res}"
        admin.sendall(b"root_admin\n")
        drain(admin, 0.5)
        print("  ✓ Admin authenticated successfully")

        print("[COMMAND TESTS] Testing General Commands...")
        
        # Test /help
        send_cmd(alice, b"/help\n")
        ok, out = recv_until(alice, "Available Commands", 2.0)
        assert ok, f"/help failed: {out}"
        print("  ✓ /help works")

        # Test /whoami
        send_cmd(alice, b"/whoami\n")
        ok, out = recv_until(alice, "You are", 2.0)
        assert ok and "alice" in out, f"/whoami failed: {out}"
        print("  ✓ /whoami works")

        # Test /users
        send_cmd(alice, b"/users\n")
        ok, out = recv_until(alice, "alice", 2.0)
        assert ok and "bob" in out, f"/users failed: {out}"
        print("  ✓ /users works")

        # Test /ping
        send_cmd(alice, b"/ping\n")
        ok, out = recv_until(alice, "pong", 2.0)
        assert ok, f"/ping failed: {out}"
        print("  ✓ /ping works")

        # Test /time
        send_cmd(alice, b"/time\n")
        ok, out = recv_until(alice, "Server time:", 2.0)
        assert ok, f"/time failed: {out}"
        print("  ✓ /time works")

        # Test /uptime
        send_cmd(alice, b"/uptime\n")
        ok, out = recv_until(alice, "Server uptime:", 2.0)
        assert ok, f"/uptime failed: {out}"
        print("  ✓ /uptime works")

        # Test /motd
        send_cmd(alice, b"/motd\n")
        ok, out = recv_until(alice, "[MOTD]", 2.0)
        assert ok, f"/motd failed: {out}"
        print("  ✓ /motd works")

        # Test /lastseen
        send_cmd(alice, b"/lastseen bob\n")
        ok, out = recv_until(alice, "online", 2.0)
        assert ok, f"/lastseen failed: {out}"
        print("  ✓ /lastseen works")

        print("[COMMAND TESTS] Testing Messaging Commands...")

        # Test /msg
        send_cmd(alice, b"/msg bob Hello Bob direct private msg\n")
        ok, out_bob = recv_until(bob, "Hello Bob direct private msg", 2.0)
        assert ok, f"/msg to bob failed: {out_bob}"
        ok, out_alice = recv_until(alice, "[PM to bob]", 2.0)
        assert ok, f"/msg confirm failed: {out_alice}"
        print("  ✓ /msg works")

        # Test /editlast
        send_cmd(alice, b"Original message before edit\n")
        send_cmd(alice, b"/editlast Edited message after edit\n")
        ok, out = recv_until(bob, "[EDITED] alice: Edited message after edit", 2.0)
        assert ok, f"/editlast failed: {out}"
        print("  ✓ /editlast works")

        # Test /deletelast
        send_cmd(alice, b"Message to delete\n")
        send_cmd(alice, b"/deletelast\n")
        ok, out = recv_until(bob, "[DELETED] alice deleted their last message", 2.0)
        assert ok, f"/deletelast failed: {out}"
        print("  ✓ /deletelast works")

        # Test /emoji
        send_cmd(alice, b"/emoji\n")
        ok, out = recv_until(alice, "Emoji Catalog", 2.0)
        assert ok, f"/emoji failed: {out}"
        print("  ✓ /emoji catalog works")

        # Test /emoji 1 (😀)
        send_cmd(alice, b"/emoji 1\n")
        ok, out = recv_until(bob, "😀", 2.0)
        assert ok, f"/emoji 1 send failed: {out}"
        print("  ✓ /emoji <id> send works")

        # Test /vanish
        send_cmd(alice, b"/vanish 2 Secret disappearing message\n")
        ok, out = recv_until(bob, "__VANISH__", 2.0)
        assert ok and "Secret disappearing message" in out, f"/vanish failed: {out}"
        print("  ✓ /vanish send works")

        print("[COMMAND TESTS] Testing Game Commands...")

        # Test /guessgame without args
        send_cmd(alice, b"/guessgame\n")
        ok, out = recv_until(alice, "Usage: /guessgame start", 2.0)
        assert ok, f"/guessgame usage failed: {out}"

        # Test /guessgame start
        send_cmd(alice, b"/guessgame start\n")
        ok, out = recv_until(alice, "Number Guessing Game started", 2.0)
        assert ok, f"/guessgame start failed: {out}"
        print("  ✓ /guessgame start works")

        # Test /guess
        send_cmd(alice, b"/guess 50\n")
        ok, out = recv_until(alice, "guessed", 2.0)
        assert ok, f"/guess failed: {out}"
        print("  ✓ /guess works")

        print("[COMMAND TESTS] Testing Customization Commands...")

        # Test /colorlist
        send_cmd(alice, b"/colorlist\n")
        ok, out = recv_until(alice, "Available colors:", 2.0)
        assert ok, f"/colorlist failed: {out}"
        print("  ✓ /colorlist works")

        # Test /setcolor
        send_cmd(alice, b"/setcolor cyan\n")
        ok, out = recv_until(alice, "color set to", 2.0)
        assert ok, f"/setcolor failed: {out}"
        print("  ✓ /setcolor works")

        print("[COMMAND TESTS] Testing Party Commands...")

        # Test /party (default)
        send_cmd(alice, b"/party\n")
        ok, out = recv_until(alice, "public", 2.0)
        assert ok, f"/party failed: {out}"
        print("  ✓ /party works")

        # Test /createparty
        send_cmd(alice, b"/createparty SECRET\n")
        ok, out = recv_until(alice, "Party 'SECRET' created", 2.0)
        assert ok, f"/createparty failed: {out}"
        print("  ✓ /createparty works")

        # Test /party (inside SECRET)
        send_cmd(alice, b"/party\n")
        ok, out = recv_until(alice, "SECRET", 2.0)
        assert ok, f"/party in SECRET failed: {out}"

        # Bob joins SECRET
        send_cmd(bob, b"/joinparty SECRET\n")
        ok, out = recv_until(bob, "Joined party 'SECRET'", 2.0)
        assert ok, f"/joinparty failed: {out}"
        print("  ✓ /joinparty works")

        # Bob leaves party
        send_cmd(bob, b"/leaveparty\n")
        ok, out = recv_until(bob, "rejoined the public room", 2.0)
        assert ok, f"/leaveparty failed: {out}"
        print("  ✓ /leaveparty works")

        print("[COMMAND TESTS] Testing Admin Commands...")

        # Test /whois
        send_cmd(admin, b"/whois alice\n")
        ok, out = recv_until(admin, "[WHOIS] User: alice", 2.0)
        assert ok, f"/whois failed: {out}"
        print("  ✓ /whois works")

        # Test /parties
        send_cmd(admin, b"/parties\n")
        ok, out = recv_until(admin, "Active Parties", 2.0)
        assert ok, f"/parties failed: {out}"
        print("  ✓ /parties works")

        # Admin joins SECRET party to test lockparty
        send_cmd(admin, b"/joinparty SECRET\n")

        # Test /lockparty
        send_cmd(admin, b"/lockparty\n")
        ok, out = recv_until(admin, "locked", 2.0)
        assert ok, f"/lockparty failed: {out}"
        print("  ✓ /lockparty works")

        # Test /invite
        send_cmd(admin, b"/invite bob\n")
        ok, out_bob = recv_until(bob, "invited to party 'SECRET'", 2.0)
        assert ok, f"/invite failed: {out_bob}"
        print("  ✓ /invite works")

        # Test /unlockparty
        send_cmd(admin, b"/unlockparty\n")
        ok, out = recv_until(admin, "unlocked", 2.0)
        assert ok, f"/unlockparty failed: {out}"
        print("  ✓ /unlockparty works")

        # Test /partyinfo
        send_cmd(admin, b"/partyinfo SECRET\n")
        ok, out = recv_until(admin, "Members in party 'SECRET'", 2.0)
        assert ok, f"/partyinfo failed: {out}"
        print("  ✓ /partyinfo works")

        # Test /mute
        send_cmd(admin, b"/mute bob\n")
        ok, out_admin = recv_until(admin, "User muted", 2.0)
        assert ok, f"/mute failed: {out_admin}"
        ok, out_bob = recv_until(bob, "You have been muted", 2.0)
        assert ok, f"/mute notice failed: {out_bob}"
        print("  ✓ /mute works")

        # Test /mutelist
        send_cmd(admin, b"/mutelist\n")
        ok, out = recv_until(admin, "bob", 2.0)
        assert ok, f"/mutelist failed: {out}"
        print("  ✓ /mutelist works")

        # Test /unmute
        send_cmd(admin, b"/unmute bob\n")
        ok, out = recv_until(admin, "User unmuted", 2.0)
        assert ok, f"/unmute failed: {out}"
        print("  ✓ /unmute works")

        # Test /pin
        send_cmd(admin, b"/pin Super Important Announcement\n")
        ok, out = recv_until(alice, "A new message has been pinned", 2.0)
        assert ok, f"/pin failed: {out}"
        print("  ✓ /pin works")

        # Test /setmotd
        send_cmd(admin, b"/setmotd Welcome to the new server MOTD!\n")
        ok, out = recv_until(alice, "MOTD updated", 2.0)
        assert ok, f"/setmotd failed: {out}"
        print("  ✓ /setmotd works")

        # Test /broadcast
        send_cmd(admin, b"/broadcast Emergency broadcast message\n")
        ok, out = recv_until(alice, "Emergency broadcast message", 2.0)
        assert ok, f"/broadcast failed: {out}"
        print("  ✓ /broadcast works")

        drain(admin, 0.3)
        # Test /log
        send_cmd(admin, b"/log\n")
        ok, out = recv_until(admin, "SERVER STARTED", 2.0)
        assert ok, f"/log failed: {out}"
        print("  ✓ /log works")

        # Test /banlist
        send_cmd(admin, b"/banlist\n")
        ok, out = recv_until(admin, "Banned IPs", 2.0)
        assert ok, f"/banlist failed: {out}"
        print("  ✓ /banlist works")

        # Test /kick
        send_cmd(admin, b"/kick bob\n")
        ok, out = recv_until(admin, "User kicked", 2.0)
        assert ok, f"/kick failed: {out}"
        print("  ✓ /kick works")

        alice.close()
        bob.close()
        admin.close()
        print("\n============================================================")
        print("   ALL COMMANDS IN /help TESTED AND VERIFIED WORKING 100%!")
        print("============================================================\n")

    finally:
        srv.terminate()
        srv.wait()

if __name__ == "__main__":
    main()
