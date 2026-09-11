"""Download a logging session from the GPS logger over USB.

Examples:
  py tools/export.py COM11                   latest session as KML
  py tools/export.py COM11 kml 20260911_143005
  py tools/export.py COM11 csv all -o everything.csv
"""
import argparse
import sys

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port")
    parser.add_argument("format", nargs="?", default="kml", choices=["kml", "gpx", "csv"])
    parser.add_argument("session", nargs="?", default="last", help="YYYYMMDD_HHMMSS, last, or all (csv only)")
    parser.add_argument("-o", "--output", help="output file (default: name reported by the device)")
    args = parser.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 10
    # Keep DTR/RTS low: toggling them resets the ESP32-C3 over native USB
    ser.dtr = False
    ser.rts = False
    ser.open()

    lines = []
    out_name = None
    with ser:
        ser.reset_input_buffer()
        ser.write(f"{args.format} {args.session}\n".encode())
        while True:
            raw = ser.readline()
            if not raw:
                sys.exit("timeout: no answer from device")
            line = raw.decode(errors="replace").rstrip("\r\n")
            if line.startswith("# end"):
                break
            if line.startswith("# file "):
                out_name = line[len("# file "):]
            elif line.startswith("#"):
                print(line)
            elif out_name:
                lines.append(line)

    if not lines:
        sys.exit("nothing exported")
    path = args.output or out_name
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(lines)} lines -> {path}")


if __name__ == "__main__":
    main()
