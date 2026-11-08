"""Tests of lantern_port.py with a pseudo-terminal in the port's place and a
thread answering as src/port.c does: python3 -m unittest discover tools. No
device is needed, and no port of the machine is opened."""
import contextlib
import io
import os
import sys
import tempfile
import threading
import tty
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lantern_port  # noqa: E402

NOT_READY = b"palette error: not ready: the settings have not loaded yet"
SIX = b"112233 445566 778899 AABBCC DDEEFF 000000"


class FakeCentral:
    """The device's side of a pseudo-terminal: reads lines from the master
    end, ignores empty ones (the newline the host sends first), and answers
    each line in turn from the script, a list of (expected line, reply
    bytes); the reply is written as the firmware writes it, a newline first
    and last, unless it is None (no reply: a word the firmware does not
    read)."""

    def __init__(self, script):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)  # no echo: what the test writes as the device must not come back as the host's
        self.path = os.ttyname(self.slave)
        self.script = list(script)
        self.lines = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        buf = b""
        while self.script:
            try:
                chunk = os.read(self.master, 4096)
            except OSError:
                return
            if not chunk:
                return
            buf += chunk
            *lines, buf = buf.split(b"\n")
            for line in lines:
                if not line:
                    continue
                self.lines.append(line)
                expected, reply = self.script.pop(0)
                if line != expected:
                    os.write(self.master, b"\nunexpected " + line + b"\n")
                elif reply is not None:
                    os.write(self.master, b"\n" + reply + b"\n")
                if not self.script:
                    return

    def close(self):
        os.close(self.master)
        os.close(self.slave)


class ExchangeTest(unittest.TestCase):
    def exchange(self, script, word, line, timeout=2.0, stale=b""):
        central = FakeCentral(script)
        try:
            if stale:
                os.write(central.master, stale)
            return lantern_port.exchange(central.path, word, line, timeout), central
        finally:
            central.close()

    def test_ask_is_answered(self):
        reply, central = self.exchange([(b"palette?", b"palette none")], "palette", "palette?")
        self.assertEqual(reply, "palette none")
        self.assertEqual(central.lines, [b"palette?"])

    def test_the_newline_first_ends_a_lost_line(self):
        # The leftover of a lost connection ("pale") plus our newline is one
        # line the firmware drops as unknown; our line follows whole.
        central = FakeCentral([(b"pale", None), (b"effect stain", b"effect ok stain")])
        try:
            fd = os.open(central.path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            os.write(fd, b"pale")
            os.close(fd)
            reply = lantern_port.exchange(central.path, "effect", "effect stain", 2.0)
        finally:
            central.close()
        self.assertEqual(reply, "effect ok stain")

    def test_log_lines_pass_and_the_reply_is_found_by_its_word(self):
        noise = (b"[00:00:01.000,000] <inf> lantern_palette: 6 colour(s) from CDC_ACM_0\n"
                 b"[00:00:01.001,000] <inf> lantern_pick: effect: pick 00000004 000000 from CDC_ACM_0\n"
                 b"[00:00:01.002,000] <inf> lantern_pa")
        reply, _ = self.exchange([(b"palette " + SIX, noise + b"\npalette ok " + SIX + b"\nlette: slots 0-1 stored")],
                                 "palette", "palette " + SIX.decode())
        self.assertEqual(reply, "palette ok " + SIX.decode())

    def test_what_arrived_before_the_line_is_read_away(self):
        # An unread reply of an earlier ask sits in the port; it is not taken
        # for ours.
        reply, _ = self.exchange([(b"palette?", b"palette " + SIX)], "palette", "palette?",
                                 stale=b"\npalette none\n")
        self.assertEqual(reply, "palette " + SIX.decode())

    def test_no_reply_at_the_deadline(self):
        reply, _ = self.exchange([(b"colour?", None)], "colour", "colour?", timeout=0.3)
        self.assertIsNone(reply)

    def test_escapes_are_stripped(self):
        reply, _ = self.exchange([(b"tempo?", b"\x1b[0mtempo on\r")], "tempo", "tempo?")
        self.assertEqual(reply, "tempo on")


class MainTest(unittest.TestCase):
    def main(self, script, argv):
        central = FakeCentral(script)
        out, err = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                status = lantern_port.main(["--port", central.path, "--timeout", "2"] + argv)
        finally:
            central.close()
        return status, out.getvalue(), err.getvalue(), central.lines

    def test_a_set_prints_ok_and_exits_0(self):
        status, out, err, lines = self.main([(b"palette 112233", b"palette ok 112233 000000 000000 000000 000000 000000")],
                                            ["palette", "112233"])
        self.assertEqual((status, out, err), (0, "palette ok 112233 000000 000000 000000 000000 000000\n", ""))
        self.assertEqual(lines, [b"palette 112233"])

    def test_an_ask_prints_the_answer_and_exits_0(self):
        status, out, _, lines = self.main([(b"intro?", b"intro none")], ["intro"])
        self.assertEqual((status, out), (0, "intro none\n"))
        self.assertEqual(lines, [b"intro?"])

    def test_an_error_reply_exits_1(self):
        status, out, _, _ = self.main([(b"effect nosuch", b"effect error: no such effect (the names: effect?)")],
                                      ["effect", "nosuch"])
        self.assertEqual((status, out), (1, "effect error: no such effect (the names: effect?)\n"))

    def test_no_reply_exits_1_and_says_why(self):
        status, out, err, _ = self.main([(b"colour?", None)], ["--timeout", "0.3", "colour"])
        self.assertEqual((status, out), (1, ""))
        self.assertIn("no reply from", err)
        self.assertIn("CONFIG_LANTERN_PORT", err)

    def test_not_ready_is_asked_again_within_wait(self):
        status, out, err, lines = self.main([(b"palette?", NOT_READY), (b"palette?", b"palette none")],
                                            ["--wait", "5", "palette"])
        self.assertEqual((status, out), (0, "palette none\n"))
        self.assertIn("not ready", err)
        self.assertIn("asking again", err)
        self.assertEqual(lines, [b"palette?", b"palette?"])

    def test_not_ready_without_wait_exits_1(self):
        status, out, _, lines = self.main([(b"palette?", NOT_READY)], ["palette"])
        self.assertEqual((status, out), (1, NOT_READY.decode() + "\n"))
        self.assertEqual(lines, [b"palette?"])

    def test_arguments_are_sent_as_given(self):
        status, _, _, lines = self.main([(b"effect stain cross-ring 2060FF", b"effect ok stain cross-ring 2060FF")],
                                        ["effect", "stain", "cross-ring", "2060FF"])
        self.assertEqual(status, 0)
        self.assertEqual(lines, [b"effect stain cross-ring 2060FF"])


class UsageTest(unittest.TestCase):
    def main(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = lantern_port.main(argv)
        return status, out.getvalue(), err.getvalue()

    def test_neither_name_nor_port(self):
        status, _, err = self.main(["palette"])
        self.assertEqual(status, 2)
        self.assertIn("--name", err)

    def test_an_argument_with_a_space_or_a_control_character(self):
        self.assertEqual(self.main(["--port", "/dev/null", "palette", "11 22"])[0], 2)
        self.assertEqual(self.main(["--port", "/dev/null", "palette", "112233\n"])[0], 2)
        self.assertEqual(self.main(["--port", "/dev/null", "palette", "カ"])[0], 2)

    def test_a_word_is_lowercase_letters(self):
        self.assertEqual(self.main(["--port", "/dev/null", "palette?"])[0], 2)
        self.assertEqual(self.main(["--port", "/dev/null", "Palette"])[0], 2)

    def test_ports_takes_nothing_else(self):
        self.assertEqual(self.main(["ports", "x"])[0], 2)
        self.assertEqual(self.main(["--name", "Corne", "ports"])[0], 2)

    def test_a_missing_port_exits_1(self):
        missing = os.path.join(tempfile.gettempdir(), "no-such-port-%d" % os.getpid())
        status, _, err = self.main(["--port", missing, "palette"])
        self.assertEqual(status, 1)
        self.assertIn("plugged in", err)


class FindTest(unittest.TestCase):
    PORTS = [
        lantern_port.Port("/dev/cu.usbmodem1", "Corne", "A1", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem2", "Twin", "B2", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem3", "Twin", "B2", "ZMK Project"),
    ]

    def test_one_port_of_the_name(self):
        self.assertEqual(lantern_port.the_port("Corne", self.PORTS), "/dev/cu.usbmodem1")

    def test_no_port_of_the_name(self):
        with self.assertRaises(lantern_port.Failure) as cm:
            lantern_port.the_port("Lily", self.PORTS)
        self.assertEqual(cm.exception.status, 1)
        self.assertIn("ports lists", str(cm.exception))

    def test_several_ports_of_the_name_need_port(self):
        with self.assertRaises(lantern_port.Failure) as cm:
            lantern_port.the_port("Twin", self.PORTS)
        self.assertEqual(cm.exception.status, 2)
        self.assertIn("/dev/cu.usbmodem2, /dev/cu.usbmodem3", str(cm.exception))


class IoregTest(unittest.TestCase):
    def test_callouts_under_usb_devices(self):
        # ioreg -a -r -t -c IOSerialBSDClient -l: each client with its
        # ancestors, one child on the path (a dict), siblings as a list.
        usb = {
            "IOObjectClass": "IOUSBHostDevice",
            "kUSBProductString": "Imprint Dongle",
            "USB Product Name": "Imprint_Dongle",
            "kUSBSerialNumberString": "E496",
            "kUSBVendorString": "ZMK Project",
            "IORegistryEntryChildren": {
                "IOObjectClass": "IOUSBHostInterface",
                "IORegistryEntryChildren": [
                    {"IOObjectClass": "AppleUSBACMControl"},
                    {"IOObjectClass": "AppleUSBACMData", "IORegistryEntryChildren": {
                        "IOObjectClass": "IOSerialBSDClient",
                        "IOCalloutDevice": "/dev/cu.usbmodem212301",
                        "IODialinDevice": "/dev/tty.usbmodem212301",
                    }},
                ],
            },
        }
        twin = {
            "IOObjectClass": "IOUSBHostDevice",
            "kUSBProductString": "Twin",
            "kUSBSerialNumberString": "DF63",
            "kUSBVendorString": "ZMK Project",
            "IORegistryEntryChildren": [
                {"IOObjectClass": "IOUSBHostInterface", "IORegistryEntryChildren": {
                    "IOObjectClass": "IOSerialBSDClient", "IOCalloutDevice": "/dev/cu.usbmodem212204"}},
                {"IOObjectClass": "IOUSBHostInterface", "IORegistryEntryChildren": {
                    "IOObjectClass": "IOSerialBSDClient", "IOCalloutDevice": "/dev/cu.usbmodem212201"}},
            ],
        }
        bluetooth = {"IOObjectClass": "IOBluetoothSerialClientModemStreamSync", "IORegistryEntryChildren": {
            "IOObjectClass": "IOSerialBSDClient", "IOCalloutDevice": "/dev/cu.Bluetooth-Incoming-Port"}}
        roots = [{"IOObjectClass": "IORegistryEntry", "IORegistryEntryChildren": [
            {"IOObjectClass": "AppleT8132USBXHCI", "IORegistryEntryChildren": [usb, twin]}, bluetooth]}]
        self.assertEqual(lantern_port.ports_ioreg(roots), [
            lantern_port.Port("/dev/cu.usbmodem212201", "Twin", "DF63", "ZMK Project"),
            lantern_port.Port("/dev/cu.usbmodem212204", "Twin", "DF63", "ZMK Project"),
            lantern_port.Port("/dev/cu.usbmodem212301", "Imprint Dongle", "E496", "ZMK Project"),
        ])

    def test_nothing(self):
        self.assertEqual(lantern_port.ports_ioreg([]), [])


class SysfsTest(unittest.TestCase):
    def test_ttys_under_usb_devices(self):
        with tempfile.TemporaryDirectory() as root:
            def mk(path, **files):
                os.makedirs(os.path.join(root, path), exist_ok=True)
                for name, text in files.items():
                    with open(os.path.join(root, path, name), "w") as f:
                        f.write(text + "\n")

            # A CDC ACM device: the tty's device link names the interface,
            # whose parent is the USB device with the descriptor strings.
            mk("devices/usb1/1-1/1-1.3", idVendor="1d50", product="Corne", serial="A1", manufacturer="ZMK Project")
            mk("devices/usb1/1-1/1-1.3/1-1.3:1.0/tty/ttyACM0")
            mk("class/tty/ttyACM0")
            os.symlink("../../../devices/usb1/1-1/1-1.3/1-1.3:1.0", os.path.join(root, "class/tty/ttyACM0/device"))
            # A hub above it carries idVendor too: the walk stops at the first.
            mk("devices/usb1/1-1", idVendor="05e3", product="Hub")
            # A UART on no USB device, and a virtual console with no device.
            mk("devices/platform/serial8250/tty/ttyS0")
            mk("class/tty/ttyS0")
            os.symlink("../../../devices/platform/serial8250", os.path.join(root, "class/tty/ttyS0/device"))
            mk("class/tty/tty0")
            self.assertEqual(lantern_port.ports_sysfs(os.path.join(root, "class/tty"), "/dev"),
                             [lantern_port.Port("/dev/ttyACM0", "Corne", "A1", "ZMK Project")])

    def test_no_sysfs(self):
        self.assertEqual(lantern_port.ports_sysfs("/no/such/class/tty"), [])


class PatternTest(unittest.TestCase):
    def test_the_word_as_a_word(self):
        p = lantern_port.reply_pattern("palette")
        self.assertTrue(p.search("palette none"))
        self.assertTrue(p.search("palette ok 112233"))
        self.assertTrue(p.search("palette"))
        self.assertFalse(p.search("<inf> lantern_palette: 6 colour(s)"))
        self.assertFalse(p.search("palette: something"))
        self.assertFalse(p.search("palettes"))
        self.assertFalse(lantern_port.reply_pattern("effect").search("lantern_pick: effect: pick 00000004"))


if __name__ == "__main__":
    unittest.main()
