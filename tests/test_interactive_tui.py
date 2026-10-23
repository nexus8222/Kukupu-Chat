import socket
import subprocess
import time
import os
import sys

PORT = 9001

def start_server():
    os.system("killall -9 server 2>/dev/null || true")
    time.sleep(0.3)
    if os.path.exists("accounts.dat"):
        os.remove("accounts.dat")
    p = subprocess.Popen(["./server"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.8)
    return p

def recv_until(s, target, timeout=2.0):
    s.settimeout(timeout)
    buf = ""
    start = time.time()
    while time.time() - start < timeout:
        try:
            chunk = s.recv(4096).decode('utf-8', errors='ignore')
            if not chunk: break
            buf += chunk
            if target in buf:
                return True, buf
        except socket.timeout:
            break
    return False, buf

def connect_client(name):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", PORT))
    s.sendall(f"{name}\n".encode())
    time.sleep(0.3)
    s.settimeout(0.5)
    try:
        s.recv(4096)
    except Exception:
        pass
    return s

def test_pm_invitation_flow():
    print("[TEST] 1. PM Invite Protocol Routing (__PMREQ__ and __PMACCEPT__)...")
    srv = start_server()
    try:
        alice = connect_client("alice")
        bob = connect_client("bob")

        # Alice sends PM invite to Bob: __PMREQ__:bob\n
        alice.sendall(b"__PMREQ__:bob\n")
        time.sleep(0.4)

        bob.settimeout(2.0)
        bob_data = bob.recv(1024).decode("utf-8", errors="ignore")
        assert "__PMREQ__:alice" in bob_data, f"Bob did not receive __PMREQ__:alice, got: {bob_data}"
        print("  ✓ Bob received __PMREQ__:alice from server")

        # Bob accepts PM invite from Alice: __PMACCEPT__:alice\n
        bob.sendall(b"__PMACCEPT__:alice\n")
        time.sleep(0.4)

        alice.settimeout(2.0)
        alice_data = alice.recv(1024).decode("utf-8", errors="ignore")
        assert "__PMACCEPT__:bob" in alice_data, f"Alice did not receive __PMACCEPT__:bob, got: {alice_data}"
        print("  ✓ Alice received __PMACCEPT__:bob from server")

        alice.close()
        bob.close()
    finally:
        srv.terminate()
        srv.wait()

def test_pm_invite_offline_user():
    print("[TEST] 2. PM Invite to Offline User Handling...")
    srv = start_server()
    try:
        alice = connect_client("alice")
        alice.sendall(b"__PMREQ__:nobody_online\n")
        time.sleep(0.4)

        alice.settimeout(2.0)
        data = alice.recv(1024).decode("utf-8", errors="ignore")
        assert "not online" in data, f"Expected not online error, got: {data}"
        print("  ✓ Server reported user is not online")
        alice.close()
    finally:
        srv.terminate()
        srv.wait()

def test_channels_and_party_switch():
    print("[TEST] 3. Channel and Party Switching...")
    srv = start_server()
    try:
        alice = connect_client("alice")
        alice.sendall(b"/createparty 7777\n")
        time.sleep(0.4)
        alice.settimeout(2.0)
        data = alice.recv(1024).decode("utf-8", errors="ignore")
        assert "7777" in data, f"Expected party code 7777, got: {data}"
        print("  ✓ Party created successfully")

        bob = connect_client("bob")
        bob.sendall(b"/joinparty 7777\n")
        time.sleep(0.4)
        bob.settimeout(2.0)
        bdata = bob.recv(1024).decode("utf-8", errors="ignore")
        assert "7777" in bdata, f"Expected joined party 7777, got: {bdata}"
        print("  ✓ Bob joined party 7777")

        alice.close()
        bob.close()
    finally:
        srv.terminate()
        srv.wait()

def test_pm_decline_flow():
    print("[TEST] 4. 1:1 PM Decline Notification (Direct & Private)...")
    srv = start_server()
    try:
        alice = connect_client("alice")
        bob = connect_client("bob")
        charlie = connect_client("charlie")

        # Alice sends PM invite to Bob
        alice.sendall(b"__PMREQ__:bob\n")
        time.sleep(0.4)
        bob.settimeout(2.0)
        bob_data = bob.recv(1024).decode("utf-8", errors="ignore")
        assert "__PMREQ__:alice" in bob_data

        # Bob declines PM invite: __PMDECLINE__:alice\n
        bob.sendall(b"__PMDECLINE__:alice\n")
        time.sleep(0.4)

        # Alice should receive 1:1 decline notification
        alice.settimeout(2.0)
        alice_data = alice.recv(1024).decode("utf-8", errors="ignore")
        assert "__PMDECLINE__:bob" in alice_data, f"Alice did not receive __PMDECLINE__:bob, got: {alice_data}"
        print("  ✓ Alice received private 1:1 __PMDECLINE__:bob")

        # Charlie (third party) must NOT receive decline notification (no broadcast)
        charlie.settimeout(0.5)
        try:
            cdata = charlie.recv(1024).decode("utf-8", errors="ignore")
            assert "__PMDECLINE__" not in cdata, "Charlie received private decline notification!"
        except socket.timeout:
            pass
        print("  ✓ Third party received no broadcast (strictly 1:1 notification)")

        alice.close()
        bob.close()
        charlie.close()
    finally:
        srv.terminate()
        srv.wait()

def test_profile_and_password_update():
    print("[TEST] 5. Profile Name & Password Update Workflow...")
    srv = start_server()
    try:
        # Register a test account
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"__REGISTER__:tui_user:StrongPass_1\n")
        ok, reg_resp = recv_until(s, "__REG_OK__")
        assert ok, f"Registration failed, got: {reg_resp}"

        # Login on same socket connection
        s.sendall(b"__LOGIN__:tui_user:StrongPass_1\n")
        ok, login_resp = recv_until(s, "__LOGIN_OK__")
        assert ok, f"Login failed, got: {login_resp}"
        time.sleep(0.3)

        # Test updating profile display name
        s.sendall(b"__SETNAME__:CyberNinja\n")
        ok, name_resp = recv_until(s, "__SETNAME_OK__:CyberNinja")
        assert ok, f"Expected __SETNAME_OK__:CyberNinja, got: {name_resp}"
        print("  ✓ Display profile name updated to 'CyberNinja'")

        # Test updating password via PBKDF2
        s.sendall(b"__CHANGEPWD__:BrandNewPass_99\n")
        ok, pwd_resp = recv_until(s, "__CHANGEPWD_OK__")
        assert ok, f"Expected __CHANGEPWD_OK__, got: {pwd_resp}"
        print("  ✓ PBKDF2 password updated successfully")
        s.close()

        # Verify login with new password on fresh socket
        s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s2.connect(("127.0.0.1", PORT))
        s2.sendall(b"__LOGIN__:tui_user:BrandNewPass_99\n")
        ok, login2_resp = recv_until(s2, "__LOGIN_OK__")
        assert ok, f"New password login failed, got: {login2_resp}"
        print("  ✓ New password authenticated successfully")
        s2.close()
    finally:
        srv.terminate()
        srv.wait()

def test_party_privacy_and_approval():
    print("[TEST] 6. Private Party Approval & Owner Notification Workflow...")
    srv = start_server()
    try:
        owner = connect_client("owner_alice")
        applicant = connect_client("bob_applicant")

        # Owner creates private party with approval required: __PARTY_CREATE__:<code >:1\n
        owner.sendall(b"__PARTY_CREATE__:8888:1\n")
        time.sleep(0.4)

        # Applicant requests to join private party: __PARTY_JOINREQ__:8888\n
        applicant.sendall(b"__PARTY_JOINREQ__:8888\n")
        ok, app_resp = recv_until(applicant, "__PARTY_PENDING__:8888")
        assert ok, f"Expected pending notice, got: {app_resp}"
        print("  ✓ Applicant received __PARTY_PENDING__:8888")

        # Owner receives join request notification
        ok, owner_resp = recv_until(owner, "__PARTY_REQ__:8888:bob_applicant")
        assert ok, f"Owner did not receive request, got: {owner_resp}"
        print("  ✓ Owner received __PARTY_REQ__:8888:bob_applicant")

        # Owner approves request: __PARTY_ACCREQ__:8888:bob_applicant\n
        owner.sendall(b"__PARTY_ACCREQ__:8888:bob_applicant\n")

        # Applicant receives joined confirmation
        ok, app_resp2 = recv_until(applicant, "__PARTY_JOINED__:8888")
        assert ok, f"Applicant did not receive joined packet, got: {app_resp2}"
        print("  ✓ Applicant received __PARTY_JOINED__:8888 upon owner approval")

        owner.close()
        applicant.close()
    finally:
        srv.terminate()
        srv.wait()

def test_party_admin_moderation():
    print("[TEST] 7. Party Admin Moderation (/partypromote, /partykick, /partyinvite)...")
    srv = start_server()
    try:
        p_owner = connect_client("p_owner")
        p_member = connect_client("p_member")
        p_outsider = connect_client("p_outsider")

        # Owner creates party 5050
        p_owner.sendall(b"/createparty 5050\n")
        time.sleep(0.4)

        # Member joins party 5050
        p_member.sendall(b"/joinparty 5050\n")
        time.sleep(0.4)

        # Owner promotes member to party admin
        p_owner.sendall(b"/partypromote p_member\n")
        ok, resp = recv_until(p_owner, "Promoted 'p_member' to party admin")
        assert ok, f"Expected promote response, got: {resp}"
        print("  ✓ Party owner promoted member to party admin")

        # Promoted member invites outsider
        p_member.sendall(b"/partyinvite p_outsider\n")
        ok, inv_data = recv_until(p_outsider, "__PARTY_INVITE__:5050")
        assert ok, f"Expected invite packet, got: {inv_data}"
        print("  ✓ Party admin sent invitation to outsider")

        # Party admin kicks member
        p_owner.sendall(b"/partykick p_member\n")
        ok, kick_data = recv_until(p_member, "removed from party")
        assert ok, f"Expected kick notification, got: {kick_data}"
        print("  ✓ Party admin kicked member from party successfully")
        print("  ✓ Party admin kicked member from party successfully")

        p_owner.close()
        p_member.close()
        p_outsider.close()
    finally:
        srv.terminate()
        srv.wait()

if __name__ == "__main__":
    print("\n============================================================")
    print("   INTERACTIVE TUI & PROTOCOL AUTOMATED TESTS")
    print("============================================================\n")
    test_pm_invitation_flow()
    test_pm_invite_offline_user()
    test_channels_and_party_switch()
    test_pm_decline_flow()
    test_profile_and_password_update()
    test_party_privacy_and_approval()
    test_party_admin_moderation()
    print("\n============================================================")
    print("   ALL INTERACTIVE TUI PROTOCOL TESTS PASSED 100%!")
    print("============================================================\n")
