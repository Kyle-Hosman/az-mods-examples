#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Serve this repository's mods to a deck or the emulator as a local repository, over HTTPS.

    python3 tools/serve-local.py --host 10.0.2.2            the emulator (QEMU's address for this machine)
    python3 tools/serve-local.py --host YOUR-COMPUTER-IP        a deck on your network

Build both mods first (azmod-pack build set-clock; azmod-pack build deck-overview). This
packages them with package URLs on this machine, writes an azmods-repo.json beside them,
makes a throwaway certificate authority and a certificate for --host, and serves
.scratch/local/ until you press Ctrl-C:

    https://HOST:PORT/azmods-repo.json      the repository to add on the deck
    https://HOST:PORT/ping                  answers 200 (stands in for the fetcher's github.com probe)
    .scratch/local/ca.pem                   the CA the deck's curl must trust

The deck only fetches https:// addresses and checks certificates, so the deck (or emulator
guest) has to trust ca.pem first: see TESTING.md. Install ca.pem only on a test deck: its
key sits unprotected in .scratch/local/.
"""
import argparse
import http.server
import os
import shutil
import ssl
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODS = ("set-clock", "deck-overview")


def run(cmd, **kw):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def make_certs(out, host):
    ca_key, ca = os.path.join(out, "ca.key"), os.path.join(out, "ca.pem")
    key, csr, crt = (os.path.join(out, n) for n in ("server.key", "server.csr", "server.pem"))
    ext = os.path.join(out, "server.ext")
    alt = f"IP:{host}" if host.replace(".", "").isdigit() else f"DNS:{host}"
    with open(ext, "w") as f:
        # the key identifiers: strict X.509 checking (Python 3.13 and later) refuses a chain
        # without them
        f.write(f"basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n"
                f"extendedKeyUsage=serverAuth\nsubjectAltName={alt}\n"
                f"subjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n")
    run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "30",
         "-subj", "/CN=az-mods-examples test CA", "-keyout", ca_key, "-out", ca,
         "-addext", "basicConstraints=critical,CA:TRUE",
         "-addext", "keyUsage=critical,keyCertSign,cRLSign",
         "-addext", "subjectKeyIdentifier=hash"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(["openssl", "req", "-newkey", "rsa:2048", "-nodes", "-subj", f"/CN={host}",
         "-keyout", key, "-out", csr], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(["openssl", "x509", "-req", "-in", csr, "-CA", ca, "-CAkey", ca_key,
         "-CAcreateserial", "-days", "30", "-extfile", ext, "-out", crt],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return crt, key


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] == "/ping":
            self.send_response(200)
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"ok")
            return
        super().do_GET()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", required=True, help="the address the deck reaches this machine at")
    ap.add_argument("--port", type=int, default=8443)
    ap.add_argument("--sdk", default=os.environ.get("AZ_SDK", os.path.join(ROOT, "..", "az-mods-sdk")),
                    help="an az-mods-sdk checkout (default: ../az-mods-sdk or $AZ_SDK)")
    args = ap.parse_args()

    pack = os.path.join(args.sdk, "tools", "azmod-pack")
    if not os.path.isfile(pack):
        sys.exit(f"no azmod-pack at {pack}: pass --sdk")
    out = os.path.join(ROOT, ".scratch", "local")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    base = f"https://{args.host}:{args.port}"

    index = os.path.join(out, "azmods-repo.json")
    for mod in MODS:
        so = os.path.join(ROOT, mod, "out", mod + ".so")
        if not os.path.isfile(so):
            sys.exit(f"{so} is missing: build it first (azmod-pack build {mod})")
        run([sys.executable, pack, "package", os.path.join(ROOT, mod), "--out", out,
             "--package-url", f"{base}/{mod}-1.0.0.azmod"], stdout=subprocess.DEVNULL)
        entry = os.path.join(out, f"{mod}-1.0.0.entry.json")
        run([sys.executable, pack, "repo", "add", index, entry, "--name", "AZ-MODS examples (local test)",
             "--maintainer", "Kyle-Hosman", "--homepage", "https://github.com/Kyle-Hosman/az-mods-examples"])
    run([sys.executable, pack, "repo", "check", index])

    crt, key = make_certs(out, args.host)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(crt, key)
    os.chdir(out)
    httpd = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    print(f"\nrepository: {base}/azmods-repo.json\nCA:         {os.path.join(out, 'ca.pem')}\n"
          f"serving {out} on port {args.port}; Ctrl-C to stop", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
