"""Tests of ARMOR-RADAR's bench tools (python -m unittest discover tests)."""
import importlib.util
import pathlib
import unittest

TOOL = pathlib.Path(__file__).resolve().parent.parent / "tools" / "frames_to_fixture.py"
spec = importlib.util.spec_from_file_location("frames_to_fixture", TOOL)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

def frame(payload_hex):
    body = ("aaff0300" + payload_hex).ljust(56, "0")
    return body + "55cc"


class FramesToFixture(unittest.TestCase):
    def test_keeps_complete_frames_and_counts_the_cut_ones(self):
        complete = frame("0e03b1a86400" + "4001")
        lines = [
            f"I (1234) armor-radar: FRAME r1 {complete}",
            "I (1300) armor-radar: radar 1: reporting",
            f"I (1400) armor-radar: FRAME r3 {complete}",
            "I (1500) armor-radar: FRAME r2 aaff03000e03",  # a line cut in the middle
            f"I (1600) armor-radar: FRAME r4 {complete}",  # there is no radar 4
        ]
        good, skipped = tool.convert(lines)
        self.assertEqual([entry[:2] for entry in good], ["r1", "r3"])
        self.assertEqual(skipped, 1)
        self.assertTrue(all(len(entry.split()[1]) == 60 for entry in good))

    def test_a_frame_without_the_manuals_header_or_tail_is_refused(self):
        wrong_header = "bbff0300" + "00" * 22 + "55cc"
        wrong_tail = "aaff0300" + "00" * 22 + "55cd"
        good, skipped = tool.convert([f"FRAME r1 {wrong_header}", f"FRAME r1 {wrong_tail}"])
        self.assertEqual((good, skipped), ([], 2))


ADOPT = pathlib.Path(__file__).resolve().parent.parent / "tools" / "adopt_node.py"
adopt_spec = importlib.util.spec_from_file_location("adopt_node", ADOPT)
adopt = importlib.util.module_from_spec(adopt_spec)
adopt_spec.loader.exec_module(adopt)


class AdoptNode(unittest.TestCase):
    def test_the_set_up_code_of_a_board_is_the_one_the_firmware_makes(self):
        # The same vector as tests/test_node.cpp: openssl and the firmware's mapping give GD8VZH4HBM for this secret and MAC.
        secret = "fleet-secret-for-tests-0123456789"
        self.assertEqual(adopt.setup_code_for(secret, "a1b2c3d4e5f6"), "GD8VZH4HBM")
        self.assertEqual(adopt.setup_code_for(secret, "A1:B2:C3:D4:E5:F6"), "GD8VZH4HBM")
        self.assertNotEqual(adopt.setup_code_for(secret, "a1b2c3d4e5f7"), "GD8VZH4HBM")
        self.assertNotEqual(adopt.setup_code_for(secret + "x", "a1b2c3d4e5f6"), "GD8VZH4HBM")
        with self.assertRaises(adopt.AdoptError):
            adopt.setup_code_for(secret, "a1b2c3")

    def test_the_fleet_settings_are_merged_section_by_section(self):
        merged = adopt.deep_merge({"ap": {"ssid": "ARMOR", "channel": 0}, "ui": {"language": "es"}}, {"ap": {"channel": 6}, "node": {"id": "n1"}})
        self.assertEqual(merged, {"ap": {"ssid": "ARMOR", "channel": 6}, "ui": {"language": "es"}, "node": {"id": "n1"}})

    def test_a_whole_adoption_against_the_stand_in_node(self):
        import json, shutil, subprocess, tempfile, time, urllib.request
        if shutil.which("node") is None:
            self.skipTest("node is not installed")
        root = pathlib.Path(__file__).resolve().parent.parent
        server = subprocess.Popen(["node", str(root / "tools" / "panel_mock.mjs"), "18131"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        temp = pathlib.Path(tempfile.mkdtemp())
        try:
            for _ in range(40):
                try:
                    urllib.request.urlopen("http://127.0.0.1:18131/api/v1/session", timeout=1).read()
                    break
                except OSError:
                    time.sleep(0.25)
            fleet = temp / "fleet.json"
            fleet.write_text(json.dumps({"ap": {"enabled": True, "ssid": "FLEET", "password": "fleet-wifi-pass"}, "mqtt": {"uri": "mqtt://192.168.0.180:18883"}, "ui": {"language": "es"}}), encoding="utf-8")
            adopt.ROOT = temp
            args = adopt.argparse.Namespace(address="http://127.0.0.1:18131", id="test-node", name="Test node", setup_code="TESTCODE", fleet=str(fleet), ip=None, netmask="255.255.255.0",
                                            gateway=None, dns=None, broker_uri=None, mqtt_username="field-node-test", mqtt_password="broker-pass-1", broker_ssh_host=None,
                                            broker_ssh_user=None, broker_ssh_key=None, restart_wait=0.0, force=False, fleet_secret_file=str(temp / "none"))
            self.assertEqual(adopt.adopt(args), 0)
            admin = (temp / "secrets" / "test-node.admin").read_text(encoding="utf-8")
            self.assertIn('user="admin"', admin)
            password = admin.split('password="')[1].split('"')[0]
            self.assertGreaterEqual(len(password), 20)
            node = adopt.Node("http://127.0.0.1:18131")
            self.assertEqual(node.call("POST", "login", {"user": "admin", "password": password})[0], 200)
            config = node.call("GET", "config")[1]["config"]
            self.assertEqual((config["node"]["id"], config["node"]["name"], config["ap"]["ssid"], config["mqtt"]["username"]), ("test-node", "Test node", "FLEET", "field-node-test"))
            with self.assertRaises(adopt.AdoptError):   # a node that already has users is not new
                adopt.adopt(args)
        finally:
            server.terminate()
            shutil.rmtree(temp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
