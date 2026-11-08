"""Tests of lantern_port.py with a pseudo-terminal in the port's place and a
thread answering as src/port.c does: python3 -m unittest discover tools. No
device is needed, and no port of the machine is opened (the one test that
looks at this machine only lists)."""
import contextlib
import fcntl
import io
import os
import select
import sys
import tempfile
import threading
import time
import tty
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lantern_port  # noqa: E402

NOT_READY = b"palette error: not ready: the settings have not loaded yet"
SIX = b"112233 445566 778899 AABBCC DDEEFF 000000"
LINE_LIMIT = 167  # src/port.c's with nine effect names (23 + 16 a name; the PRELUDE's margin covers 62)
WORDS = (b"palette", b"effect", b"intro", b"tempo")


class FakeCentral:
    """The device's side of a pseudo-terminal, fed as src/port.c's feed()
    is: bytes into a line up to LINE_LIMIT, an overlong line dropped at its
    newline without a reply, an empty line nothing. A complete line that
    starts with none of the port's words (arguments() in port.c) gets no
    reply and uses no script entry; every other one is answered in turn from
    the script, a list of (expected line, reply bytes), the reply written as
    the firmware writes it, a newline first and last, unless it is None (no
    reply). Reads and writes on the master end go through a select with a
    timeout, never a blocking call: on macOS a close of the master waits for
    a read or a write blocked on it."""

    def __init__(self, script):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)  # no echo: what the test writes as the device must not come back as the host's
        fcntl.fcntl(self.master, fcntl.F_SETFL, fcntl.fcntl(self.master, fcntl.F_GETFL) | os.O_NONBLOCK)
        self.path = os.ttyname(self.slave)
        self.script = list(script)
        self.lines = []
        self.dropped = 0
        self.stop = False
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        text, overflow = b"", False
        while not self.stop:
            # A select with a timeout, never a blocking read: on macOS a
            # close of the master would wait for a read blocked on it.
            if not select.select([self.master], [], [], 0.05)[0]:
                continue
            try:
                chunk = os.read(self.master, 4096)
            except OSError:
                return
            if not chunk:
                return
            for c in chunk:
                if c in (0x0A, 0x0D):
                    if overflow:
                        overflow = False
                        self.dropped += 1
                    elif text:
                        self.answer(text)
                    text = b""
                elif overflow:
                    pass
                elif len(text) == LINE_LIMIT:
                    overflow, text = True, b""
                else:
                    text += bytes([c])

    def answer(self, line):
        self.lines.append(line)
        word = line.split(b" ")[0].rstrip(b"?")
        if word not in WORDS or not self.script:
            return
        expected, reply = self.script.pop(0)
        if line != expected:
            self.write(b"\nunexpected " + line + b"\n")
        elif reply is not None:
            self.write(b"\n" + reply + b"\n")

    def write(self, data):
        while data and not self.stop:
            if not select.select([], [self.master], [], 0.05)[1]:
                continue
            try:
                data = data[os.write(self.master, data):]
            except BlockingIOError:
                pass

    def cut(self):
        """The device gone: its side of the pty closed, so the host's reads
        on the slave end (macOS: end of file; Linux: EIO)."""
        self.stop = True
        self.thread.join(5.0)
        os.close(self.master)
        self.master = None

    def close(self):
        if self.master is not None:
            self.cut()
        os.close(self.slave)


class ExchangeTest(unittest.TestCase):
    def exchange(self, script, word, line, timeout=2.0, stale=b"", leftover=b""):
        central = FakeCentral(script)
        try:
            if stale:  # the device's unread output
                os.write(central.master, stale)
            if leftover:  # a host's line that never got its newline
                fd = os.open(central.path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                os.write(fd, leftover)
                os.close(fd)
            return lantern_port.exchange(central.path, word, line, timeout), central
        finally:
            central.close()

    def test_ask_is_answered(self):
        reply, central = self.exchange([(b"palette?", b"palette none")], "palette", "palette?")
        self.assertEqual(reply, "palette none")
        self.assertEqual(central.lines, [b"palette?"])

    def test_the_prelude_drops_a_leftover_instead_of_running_it(self):
        # A set typed in a terminal and never sent sits in the central's
        # buffer; a bare newline would complete it, set the palette and
        # answer `palette ok ...`, which would pass for our ask's reply.
        reply, central = self.exchange([(b"palette?", b"palette none")], "palette", "palette?",
                                       leftover=b"palette 112233")
        self.assertEqual(reply, "palette none")
        self.assertEqual(central.lines, [b"palette?"], "the leftover never became a line")
        self.assertEqual(central.dropped, 1)

    def test_the_prelude_drops_the_shortest_leftover_that_is_a_command(self):
        # `palette` alone, 7 bytes: with fewer spaces than LINE_LIMIT - 6 the
        # line would complete as an ask and its reply come before ours.
        reply, central = self.exchange([(b"palette 445566", b"palette ok 445566 000000 000000 000000 000000 000000")],
                                       "palette", "palette 445566", leftover=b"palette")
        self.assertEqual(reply, "palette ok 445566 000000 000000 000000 000000 000000")
        self.assertEqual(central.lines, [b"palette 445566"])
        self.assertGreater(len(lantern_port.PRELUDE) - 1, LINE_LIMIT, "the spaces alone must overflow the buffer")

    def test_the_prelude_drops_a_leftover_already_overlong(self):
        reply, central = self.exchange([(b"effect stain", b"effect ok stain")], "effect", "effect stain",
                                       leftover=b"x" * (LINE_LIMIT + 40))
        self.assertEqual(reply, "effect ok stain")
        self.assertEqual(central.lines, [b"effect stain"])

    def test_log_lines_pass_and_the_reply_is_found_at_the_start_of_its_line(self):
        noise = (b"[00:00:01.000,000] <inf> lantern_palette: 6 colour(s) from CDC_ACM_0\n"
                 b"[00:00:01.001,000] <inf> lantern_pick: effect: pick 00000004 000000 from CDC_ACM_0\n"
                 b"uart:~$ palette " + SIX + b"\n"
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

    def test_a_device_that_goes_away_is_tried_again_within_wait(self):
        # The device vanishes while the host waits for the reply (the central
        # rebooted, a KVM switch cut its power) and comes back as another
        # port; with --wait left the host looks for it again.
        gone = FakeCentral([(b"tempo?", None)])
        back = FakeCentral([(b"tempo?", b"tempo on")])
        out, err = io.StringIO(), io.StringIO()
        found = []

        def ports():
            current = back if gone.master is None else gone
            found.append(current.path)
            return [lantern_port.Port(current.path, "Corne", "A1", "ZMK Project")]

        def cut():
            end = time.monotonic() + 5.0  # a host that never writes must not keep this thread alive
            while len(gone.lines) < 1 and time.monotonic() < end:
                select.select([], [], [], 0.01)
            gone.cut()

        real = lantern_port.usb_serial_ports
        lantern_port.usb_serial_ports = ports
        try:
            threading.Thread(target=cut, daemon=True).start()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                status = lantern_port.main(["--name", "Corne", "--timeout", "2", "--wait", "5", "tempo"])
        finally:
            lantern_port.usb_serial_ports = real
            gone.close()
            back.close()
        self.assertEqual((status, out.getvalue()), (0, "tempo on\n"))
        self.assertIn("lost", err.getvalue())
        self.assertIn("trying again", err.getvalue())
        self.assertEqual(found[0], gone.path)
        self.assertEqual(found[-1], back.path)
        self.assertEqual(back.lines, [b"tempo?"])


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

    def test_the_shape_of_a_reply_is_refused(self):
        for first in ("none", "ok", "error:"):
            status, _, err = self.main(["--port", "/dev/null", "effect", first, "x"])
            self.assertEqual(status, 2, first)
            self.assertIn("shape of a reply", err)

    def test_ports_takes_nothing_else(self):
        self.assertEqual(self.main(["ports", "x"])[0], 2)
        self.assertEqual(self.main(["--name", "Corne", "ports"])[0], 2)

    def test_a_missing_port_exits_1(self):
        missing = os.path.join(tempfile.gettempdir(), "no-such-port-%d" % os.getpid())
        status, _, err = self.main(["--port", missing, "palette"])
        self.assertEqual(status, 1)
        self.assertIn("plugged in", err)

    def test_a_port_that_cannot_be_opened_is_not_tried_again(self):
        # A directory opens with EISDIR, as a port without permission would
        # with EACCES: not a device that is away, so --wait does not apply.
        start = time.monotonic()
        status, _, err = self.main(["--port", tempfile.gettempdir(), "--wait", "5", "palette"])
        self.assertEqual(status, 1)
        self.assertIn("cannot open", err)
        self.assertNotIn("trying again", err)
        self.assertLess(time.monotonic() - start, 2.0)

    def test_seconds_are_finite_and_a_timeout_is_more_than_0(self):
        for bad in ("nan", "inf", "-1", "x"):
            with self.assertRaises(SystemExit, msg=bad):
                with contextlib.redirect_stderr(io.StringIO()):
                    lantern_port.parser().parse_args(["--wait", bad, "palette"])
        for bad in ("0", "nan", "inf", "-1", "1e400"):
            with self.assertRaises(SystemExit, msg=bad):
                with contextlib.redirect_stderr(io.StringIO()):
                    lantern_port.parser().parse_args(["--timeout", bad, "palette"])
        args = lantern_port.parser().parse_args(["--wait", "0", "--timeout", "0.5", "palette"])
        self.assertEqual((args.wait, args.timeout), (0.0, 0.5))
        self.assertEqual(lantern_port.parser().parse_args(["--timeout", "1e-10", "palette"]).timeout, 1e-10)


class FindTest(unittest.TestCase):
    PORTS = [
        lantern_port.Port("/dev/cu.usbmodem1", "Corne", "A1", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem2", "Twin", "B2", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem3", "Twin", "B2", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem4", "Pair", "C3", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem5", "Pair", "C4", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem6", "Blank", "", "ZMK Project"),
        lantern_port.Port("/dev/cu.usbmodem7", "Blank", "", "ZMK Project"),
    ]

    def test_one_port_of_the_name(self):
        self.assertEqual(lantern_port.the_port("Corne", self.PORTS), "/dev/cu.usbmodem1")

    def test_one_device_with_two_ports_gives_its_first(self):
        self.assertEqual(lantern_port.the_port("Twin", self.PORTS), "/dev/cu.usbmodem2")

    def test_no_port_of_the_name(self):
        with self.assertRaises(lantern_port.Failure) as cm:
            lantern_port.the_port("Lily", self.PORTS)
        self.assertEqual(cm.exception.status, 1)
        self.assertIn("ports lists", str(cm.exception))

    def test_two_devices_of_the_name_need_port(self):
        with self.assertRaises(lantern_port.Failure) as cm:
            lantern_port.the_port("Pair", self.PORTS)
        self.assertEqual(cm.exception.status, 2)
        self.assertIn("/dev/cu.usbmodem4, /dev/cu.usbmodem5", str(cm.exception))

    def test_two_ports_without_serials_need_port(self):
        with self.assertRaises(lantern_port.Failure) as cm:
            lantern_port.the_port("Blank", self.PORTS)
        self.assertEqual(cm.exception.status, 2)


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
            # A USB-serial converter: the tty's device is the port under the interface.
            mk("devices/usb1/1-2", idVendor="0403", product="FT232R", serial="FT1", manufacturer="FTDI")
            mk("devices/usb1/1-2/1-2:1.0/ttyUSB0/tty/ttyUSB0")
            mk("class/tty/ttyUSB0")
            os.symlink("../../../devices/usb1/1-2/1-2:1.0/ttyUSB0", os.path.join(root, "class/tty/ttyUSB0/device"))
            # A UART on no USB device, and a virtual console with no device.
            mk("devices/platform/serial8250/tty/ttyS0")
            mk("class/tty/ttyS0")
            os.symlink("../../../devices/platform/serial8250", os.path.join(root, "class/tty/ttyS0/device"))
            mk("class/tty/tty0")
            self.assertEqual(lantern_port.ports_sysfs(os.path.join(root, "class/tty"), "/dev"), [
                lantern_port.Port("/dev/ttyACM0", "Corne", "A1", "ZMK Project"),
                lantern_port.Port("/dev/ttyUSB0", "FT232R", "FT1", "FTDI"),
            ])

    def test_no_sysfs(self):
        self.assertEqual(lantern_port.ports_sysfs("/no/such/class/tty"), [])


class ThisMachineTest(unittest.TestCase):
    def test_listing_this_machine_works(self):
        # ioreg on macOS, sysfs on Linux, as they are here: the output parses
        # and every entry is a Port; whatever devices there are (CI has none).
        # Nothing is opened.
        ports = lantern_port.usb_serial_ports()
        self.assertIsInstance(ports, list)
        for p in ports:
            self.assertIsInstance(p, lantern_port.Port)
            self.assertTrue(p.path.startswith("/dev/"), p)


class PatternTest(unittest.TestCase):
    def test_the_word_at_the_start_of_the_line(self):
        p = lantern_port.reply_pattern("palette")
        self.assertTrue(p.match("palette none"))
        self.assertTrue(p.match("palette ok 112233"))
        self.assertTrue(p.match("palette"))
        self.assertFalse(p.match("<inf> lantern_palette: 6 colour(s)"))
        self.assertFalse(p.match("palette: something"))
        self.assertFalse(p.match("palettes"))
        self.assertFalse(p.match("uart:~$ palette 112233"))
        self.assertFalse(lantern_port.reply_pattern("effect").match("lantern_pick: effect: pick 00000004"))


if __name__ == "__main__":
    unittest.main()
