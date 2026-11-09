#!/usr/bin/env python3
"""A host for the port: one line to a keyboard's central over USB (a split's
central, or the keyboard itself when it is not a split), and its reply
printed (README, The host).

  lantern_port.py (--name <product string> | --port <device>) [--wait S] [--timeout S] <word> [<argument> ...]
  lantern_port.py ports

The word and its arguments are the line src/port.c reads, passed as they are:
`palette RRGGBB ...`, `effect <name> ... [RRGGBB]`, `intro <name>`, `tempo on`;
the word alone asks (`palette?`). The reply goes to stdout: `<word> ok ...`
for a set, the values for an ask, `<word> none`, or `<word> error: <reason>`.
This tool knows no name and no colour: the firmware refuses what it does not
know and says why, so a new effect needs nothing here. Exit 0 for `ok` or an
ask's answer, 1 for an error reply, no reply or no device, 2 for bad usage.

The device is the central, found by its USB product string
(CONFIG_ZMK_KEYBOARD_NAME; on a stock split shield the central's), `--name
Corne`, among this machine's USB serial ports: macOS's I/O Registry (ioreg)
or Linux's sysfs, never by a /dev name, which follows the USB socket, nor by
the USB IDs, which every ZMK keyboard shares. A device with several CDC ACM
ports answers on every one, so its first port is taken; a name that several
devices carry (two keyboards, told apart by their serial numbers) needs
`--port <device>` instead; `ports` lists every USB serial port with its
product string, serial and manufacturer. `--wait S` first polls up to S
seconds for the device (plugged in, rebooting, hidden by a KVM switch), tries
again within the same S when it goes away meanwhile, and asks again while it
answers `not ready` (its USB up before its settings loaded), but not a port
that cannot be opened for another reason (no permission, another program
holding it); `--timeout S` (3) bounds the write and then the reply, after up
to DRAIN_LIMIT_S of reading away what arrived earlier.

The host's side of the port, which a host of its own follows too:

- The port is opened raw, 8N1 at 115200, DTR and RTS raised, and 115200 is
  the only rate a host ever sets: 1200 baud is zmk-vfx-beacon's bootloader entry
  on a central that builds it, 2400 its screen dump.
- The line is preceded by more spaces than the central's line buffer holds
  and a newline (PRELUDE): whatever a lost connection left in the buffer,
  with the spaces, overflows the buffer, and the central drops an overlong
  line at its newline without a reply (src/port.c, feed()), where a bare
  newline would complete the leftover, run it when it is a command (a set
  typed in a terminal and never sent) and answer it, the answer passing
  for ours. One line at a time, its reply awaited: the central keeps two
  lines waiting and drops a third.
- The reply starts on a line of its own, and is read at the start of a line
  by its word: in a logging image the log thread's lines pass by, and one
  may be finished after the reply. Whatever arrived before the line is read
  away first.
- `ok` means the central stored the value and queued the halves' invocations;
  a half that is away gets them a second after its next battery report.
- Nothing shaped like a reply is ever sent (`<word> ok ...`, `<word> none`,
  `<word> error: ...`; refused here as usage): the central ignores such a
  line, so an echoing terminal cannot start a loop, but a host that answered
  replies would. One reader of the port at a time: another program reading
  it takes the reply.

Standard library only, Python 3.9 (macOS's /usr/bin/python3): termios, so
macOS and Linux, where the port needs the permission to open it (the dialout
group, or a udev rule). Tests: python3 -m unittest discover tools, on a
pseudo-terminal in the port's place.
"""
import argparse
import collections
import errno
import fcntl
import math
import os
import plistlib
import re
import select
import struct
import subprocess
import sys
import termios
import time

BAUD = termios.B115200
POLL_S = 0.5  # --wait's poll, and the pause before asking or trying again
QUIET_S = 0.2  # what arrived before the line is read away until this long without a byte...
DRAIN_LIMIT_S = 3.0  # ...or this long in all (a logging image never falls silent)
# More than src/port.c's LINE_LIMIT (167 with nine effect names: 23 + 16 a
# name of the longest table), so a leftover of any length overflows with it.
PRELUDE = b" " * 1024 + b"\n"
PORTS_WORD = "ports"
WORDS = ("palette", "effect", "intro", "tempo")  # the port's words today, for the messages only
REPLY_SHAPES = ("ok", "none", "error:")  # src/port.c's arguments(): a line with one of these first is no command
WORD = re.compile(r"[a-z]+")
ARGUMENT = re.compile(r"[!-~]+")  # printable ASCII without a space: the line is ASCII, split at spaces
ESCAPES = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|[@-Z\\-_])|[\x00-\x08\x0b-\x1f\x7f]")

Port = collections.namedtuple("Port", "path product serial manufacturer")


class Failure(Exception):
    """A message for stderr and an exit status; `retry` marks the failures
    --wait tries again, those a device that is away or rebooting causes (not
    found, gone, not there to open), never a port that cannot be opened for
    another reason (no permission, another program holding it)."""

    def __init__(self, message, status=1, retry=False):
        super().__init__(message)
        self.status = status
        self.retry = retry


GONE = (errno.ENOENT, errno.ENXIO, errno.EIO, errno.ENODEV)  # the device left between the lookup and the open


def ioreg_serial():
    """ioreg's IOSerialBSDClient entries with their ancestors (-r -t), as
    plists."""
    try:
        raw = subprocess.run(["ioreg", "-a", "-r", "-t", "-c", "IOSerialBSDClient", "-l"],
                             capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        raise Failure("ioreg failed: %s" % e)
    try:
        roots = plistlib.loads(raw) if raw.strip() else []
    except Exception as e:  # plistlib raises expat's and its own errors on malformed output
        raise Failure("ioreg's output is no plist: %s" % e)
    return roots if isinstance(roots, list) else [roots]


def ports_ioreg(roots):
    """The USB serial ports in ioreg's output: each IOCalloutDevice (/dev/cu.*)
    under an IOUSBHostDevice, with that device's descriptor strings
    (kUSBProductString, not "USB Product Name", which macOS may have
    sanitised). A port under no USB device (Bluetooth, the debug console) is
    left out."""
    found = []

    def walk(node, usb):
        if node.get("IOObjectClass") == "IOUSBHostDevice":
            usb = node
        if "IOCalloutDevice" in node and usb is not None:
            found.append(Port(str(node["IOCalloutDevice"]), str(usb.get("kUSBProductString", "")),
                              str(usb.get("kUSBSerialNumberString", "")), str(usb.get("kUSBVendorString", ""))))
        kids = node.get("IORegistryEntryChildren", [])
        for kid in [kids] if isinstance(kids, dict) else kids:  # -t: an ancestor's one child on the path is a dict
            walk(kid, usb)

    for root in roots:
        walk(root, None)
    return sorted(found)


def attribute(directory, name):
    try:
        with open(os.path.join(directory, name), encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    except OSError:
        return ""


def ports_sysfs(tty_class="/sys/class/tty", dev="/dev"):
    """The USB serial ports in Linux's sysfs: each tty whose device sits under
    a USB device (the directory with idVendor, walking up from the tty's
    `device` link, an interface's directory), with that device's product,
    serial and manufacturer attributes. A tty with no device (a virtual
    console) or with none on USB (a UART) is left out."""
    found = []
    try:
        names = sorted(os.listdir(tty_class))
    except OSError:
        return found
    for name in names:
        d = os.path.realpath(os.path.join(tty_class, name, "device"))
        if not os.path.isdir(d):
            continue
        while True:
            if os.path.exists(os.path.join(d, "idVendor")):
                found.append(Port(os.path.join(dev, name), attribute(d, "product"), attribute(d, "serial"),
                                  attribute(d, "manufacturer")))
                break
            parent = os.path.dirname(d)
            if parent == d:
                break
            d = parent
    return found


def usb_serial_ports():
    if sys.platform == "darwin":
        return ports_ioreg(ioreg_serial())
    return ports_sysfs()


def the_port(name, ports):
    """The port of the device whose product string is name: its first when
    the device has several (src/port.c answers on every CDC ACM port), or
    Failure: no such device (1), or several, told apart by their serial
    numbers (2: --port names one)."""
    hits = [p for p in ports if p.product == name]
    if not hits:
        raise Failure('no USB serial port of a device named "%s" (%s lists what there is)' % (name, PORTS_WORD),
                      retry=True)
    serials = set(p.serial for p in hits)
    if len(hits) > 1 and (len(serials) > 1 or "" in serials):
        raise Failure('%d ports carry the name "%s": --port names one of %s'
                      % (len(hits), name, ", ".join(p.path for p in hits)), 2)
    return hits[0].path


def find_port(args, deadline):
    """The port to write, --port as given or --name's, polled for until
    deadline (--wait); a name several devices carry is refused at once."""
    while True:
        try:
            if args.port:
                if os.path.exists(args.port):
                    return args.port
                raise Failure("no %s: is the device plugged in?" % args.port, retry=True)
            return the_port(args.name, usb_serial_ports())
        except Failure as e:
            if not e.retry or time.monotonic() >= deadline:
                raise
        time.sleep(min(POLL_S, max(deadline - time.monotonic(), 0.0)))


def open_port(path):
    """The port opened raw, 8N1 at 115200, DTR and RTS raised, without HUPCL
    (closing leaves DTR up)."""
    try:
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as e:
        raise Failure("cannot open %s: %s" % (path, e.strerror or e), retry=e.errno in GONE)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0  # iflag
        attrs[1] = 0  # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
        attrs[3] = 0  # lflag
        attrs[4] = attrs[5] = BAUD
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        try:
            fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack("I", termios.TIOCM_DTR | termios.TIOCM_RTS))
        except OSError as e:
            if e.errno not in (errno.ENOTTY, errno.EINVAL):  # a pseudo-terminal (the tests) has no modem lines
                raise
    except (OSError, termios.error) as e:  # termios.error is no OSError
        os.close(fd)
        raise Failure("cannot set up %s: %s" % (path, e.args[-1]))
    return fd


def read_some(fd, path, timeout):
    """The bytes that arrive within timeout seconds (b"" for none)."""
    ready, _, _ = select.select([fd], [], [], max(timeout, 0.0))
    if not ready:
        return b""
    try:
        chunk = os.read(fd, 65536)
    except (BlockingIOError, InterruptedError):
        return b""
    except OSError as e:
        raise Failure("lost %s: %s" % (path, e.strerror or e), retry=True)
    if not chunk:
        raise Failure("lost %s: end of file" % path, retry=True)
    return chunk


def drain(fd, path):
    """Reads away what arrived before the line (a logging image's lines, a
    reply nobody read), until QUIET_S without a byte or DRAIN_LIMIT_S in all."""
    limit = time.monotonic() + DRAIN_LIMIT_S
    while True:
        wait = min(QUIET_S, limit - time.monotonic())
        if wait <= 0 or not read_some(fd, path, wait):
            return


def write_all(fd, path, data, deadline):
    """Writes the whole of data, waiting for room until deadline: the port is
    open O_NONBLOCK, so a write may take part of it, or none while the USB
    stack's buffer is full."""
    while data:
        try:
            data = data[os.write(fd, data):]
        except (BlockingIOError, InterruptedError):
            pass
        except OSError as e:
            raise Failure("lost %s: %s" % (path, e.strerror or e), retry=True)
        if data:
            left = deadline - time.monotonic()
            if left <= 0:
                raise Failure("%s took no more of the line" % path)
            select.select([], [fd], [], min(left, POLL_S))


def clean(raw):
    return ESCAPES.sub("", raw.decode("utf-8", "replace")).rstrip()


def reply_pattern(word):
    """The reply to the word at the start of a line: the firmware writes it
    on a line of its own, whole (a logging image's log thread, interrupted
    mid-line, finishes its line after it), so a line that carries the word
    elsewhere (a log module's name, `lantern_palette:`, a log line, `effect:
    pick ...`, a shell's echo of our own line) is not it."""
    return re.compile(r"^" + re.escape(word) + r"(?: |$)")


def read_reply(fd, path, word, deadline):
    """The first line from the port that is the word's reply, or None at the
    deadline."""
    pattern = reply_pattern(word)
    buf = b""
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            return None
        buf += read_some(fd, path, min(left, POLL_S))
        *lines, buf = buf.split(b"\n")
        for raw in lines:
            text = clean(raw)
            if pattern.match(text):
                return text


def exchange(path, word, line, timeout):
    """Writes the line on the port, the PRELUDE before it, and returns the
    reply to the word, or None after timeout seconds (the write, then the
    reply)."""
    fd = open_port(path)
    try:
        drain(fd, path)
        write_all(fd, path, PRELUDE + (line + "\n").encode("ascii"), time.monotonic() + timeout)
        return read_reply(fd, path, word, time.monotonic() + timeout)
    finally:
        os.close(fd)


def ask(args):
    line = args.word + ("?" if not args.arguments else " " + " ".join(args.arguments))
    not_ready = args.word + " error: not ready"
    deadline = time.monotonic() + args.wait
    while True:
        try:
            path = find_port(args, deadline)
            reply = exchange(path, args.word, line, args.timeout)
        except Failure as e:
            # The device went away between the lookup and the reply (a KVM
            # switch cut its power, it rebooted): --wait's budget covers it.
            if not e.retry or time.monotonic() >= deadline:
                raise
            print("lantern_port: %s; trying again" % e, file=sys.stderr)
            time.sleep(POLL_S)
            continue
        if reply is None or not reply.startswith(not_ready) or time.monotonic() >= deadline:
            break
        print("lantern_port: %s; asking again" % reply, file=sys.stderr)
        time.sleep(POLL_S)
    if reply is None:
        raise Failure("no reply from %s within %g s: an image without CONFIG_LANTERN_PORT ignores the line, and so "
                      "does a word that is none of %s; another program reading the port takes the reply"
                      % (path, args.timeout, ", ".join(WORDS)))
    print(reply)
    if reply.startswith(args.word + " error"):
        return 1
    if args.arguments and not reply.startswith(args.word + " ok "):
        return 1
    return 0


def list_ports():
    ports = usb_serial_ports()
    if not ports:
        print("lantern_port: no USB serial port on this machine", file=sys.stderr)
    for p in ports:
        print('%s  "%s"  serial=%s  by "%s"' % (p.path, p.product, p.serial, p.manufacturer))
    return 0


def seconds(text, positive=False):
    """A finite number of seconds, 0 or more, or more than 0 when positive (a
    0 timeout would write the line and never read the reply)."""
    try:
        value = float(text)
    except ValueError:
        value = math.nan
    if not math.isfinite(value) or value < 0 or (positive and value == 0):
        raise argparse.ArgumentTypeError("seconds, %s, not %r" % ("more than 0" if positive else "0 or more", text))
    return value


def timeout_seconds(text):
    return seconds(text, positive=True)


def parser():
    p = argparse.ArgumentParser(
        prog="lantern_port.py",
        description="Write one line on the USB serial port of a keyboard running zmk-vfx-lantern (its central: a "
                    "split's, or the keyboard itself when it is not a split) "
                    "and print the reply: WORD and its ARGUMENTs as the port takes them (palette RRGGBB ..., "
                    "effect NAME ... [RRGGBB], intro NAME, tempo on|off; the README's The palette and The picks), "
                    "or WORD alone to ask. 115200 is the only rate it sets. 'ports' instead lists this machine's "
                    "USB serial ports with their product strings.",
        epilog="Exit status: 0 for '<word> ok' or an ask's answer, 1 for '<word> error', no reply or no device, "
               "2 for bad usage.",
    )
    which = p.add_mutually_exclusive_group()
    which.add_argument("--name", metavar="PRODUCT",
                       help="the keyboard's USB product string (CONFIG_ZMK_KEYBOARD_NAME, the central's; "
                            "'ports' shows it)")
    which.add_argument("--port", metavar="DEVICE",
                       help="the port itself (/dev/cu.usbmodem..., /dev/ttyACM0): when several devices carry the name")
    p.add_argument("--wait", type=seconds, default=0.0, metavar="S",
                   help="first poll up to S seconds, every %g s, for the device, and within the same S try again "
                        "when it goes away and ask again while it answers 'not ready' (a central just booted: its "
                        "USB up before its settings; default 0); a port that cannot be opened for another reason "
                        "(no permission, another program holding it) is not tried again" % POLL_S)
    p.add_argument("--timeout", type=timeout_seconds, default=3.0, metavar="S",
                   help="give up on writing the line, and then on the reply, after S seconds each (default 3; "
                        "before the write, up to %g s of reading away what arrived earlier)" % DRAIN_LIMIT_S)
    p.add_argument("word", metavar="WORD", help="palette, effect, intro or tempo (the port's words), or ports")
    p.add_argument("arguments", nargs="*", metavar="ARGUMENT",
                   help="the word's arguments, as the port takes them; none asks")
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.word == PORTS_WORD:
            if args.arguments or args.name or args.port:
                raise Failure("%s takes nothing else" % PORTS_WORD, 2)
            return list_ports()
        if not args.name and not args.port:
            raise Failure("--name <product string> or --port <device> names the keyboard (%s lists this machine's "
                          "USB serial ports)" % PORTS_WORD, 2)
        if not WORD.fullmatch(args.word):
            raise Failure("a word is lowercase letters (%s), not %r" % (", ".join(WORDS), args.word), 2)
        for a in args.arguments:
            if not ARGUMENT.fullmatch(a):
                raise Failure("an argument is printable ASCII without a space, not %r" % a, 2)
        if args.arguments and args.arguments[0] in REPLY_SHAPES:
            raise Failure("'%s %s' is the shape of a reply, which the firmware ignores: a set names values, a pick "
                          "one or more of the built names, and there is no none" % (args.word, args.arguments[0]), 2)
        return ask(args)
    except Failure as e:
        print("lantern_port: %s" % e, file=sys.stderr)
        return e.status
    except KeyboardInterrupt:
        print("lantern_port: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
