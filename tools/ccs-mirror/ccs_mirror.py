#!/usr/bin/env python3
# ***************************************************************************
# * This file is part of NUSspli.                                           *
# * Copyright (c) 2026 V10lator <v10lator@myway.de>                         *
# *                                                                         *
# * This program is free software; you can redistribute it and/or modify   *
# * it under the terms of the GNU General Public License as published by    *
# * the Free Software Foundation; either version 3 of the License, or       *
# * (at your option) any later version.                                     *
# *                                                                         *
# * This program is distributed in the hope that it will be useful,         *
# * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
# * GNU General Public License for more details.                            *
# *                                                                         *
# * You should have received a copy of the GNU General Public License along *
# * with this program; if not, If not, see <http://www.gnu.org/licenses/>.  *
# ***************************************************************************
"""
A mirror of the secure content server of the Wii U, answered from the public CDN.

The Wii U Download Management fetches title files from
https://ccs.wup.shop.nintendo.net/ccs/download/<title ID>/<file>, and that server
answers 404 for files the public CDN at
http://ccs.cdn.wup.shop.nintendo.net/ccs/download/<title ID>/<file> still serves
(seen for the update of Wii Sports Club, 105-0955). With the console's DNS
pointing ccs.wup.shop.nintendo.net here, this server answers the same paths from
the public CDN.

It only passes on what the public CDN serves to anyone: the title files, and the
ticket (cetk) of free content like updates. A paid title gets no ticket from
here, the CDN answers 404 for it just like before.

The console checks the certificate against the CAs of Nintendo. Firmware 5.5.5
and newer accepts a certificate made with Pretendo's SSSL tool from the
console's own "Nintendo CA - G3" (see README.md), and only over TLS 1.0/1.1 with
old ciphers, which modern OpenSSL disables unless asked for them explicitly.

Python 3 standard library only.
"""

import argparse
import http.server
import ipaddress
import logging
import re
import socketserver
import ssl
import sys
import os
import struct
import urllib.error
import urllib.request
import warnings

# The icon maker sits next to this file. Saying so makes the import work
# however the server was started, not only when the folder of the script
# happens to be on the path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import idbe

UPSTREAM = "http://ccs.cdn.wup.shop.nintendo.net/ccs/download/"
# The icon of a title, which the Download Management fetches before it takes
# the title at all: a 404 here is 105-1017 and nothing is downloaded. Nintendo
# still answers for it, so a console that fails here is usually asking another
# server, see README.md.
IDBE_UPSTREAM = "https://idbe-wup.cdn.nintendo.net/icondata/"
USER_AGENT = "NUSspli-ccs-mirror"
CHUNK = 64 * 1024

# Title ID, then the file: tmd, tmd.<version>, cetk, or a content ID with or
# without .h3. Everything else is answered with 404 and logged, so a path the
# console asks for that this does not know shows up in the log.
PATH_RE = re.compile(
    r"^/ccs/download/([0-9a-fA-F]{16})/(tmd(?:\.[0-9]{1,5})?|cetk|[0-9a-fA-F]{8}(?:\.h3)?)$"
)

# The icon: two hex digits of the title ID, then the title ID with or without
# its version. See https://wiiubrew.org/wiki/Nn_idbe.rpl for the template.
IDBE_PATH_RE = re.compile(
    r"^/icondata/([0-9a-fA-F]{2})/([0-9a-fA-F]{16}(?:-[0-9]{1,5})?\.idbe)$"
)

log = logging.getLogger("ccs-mirror")


class MirrorHandler(http.server.BaseHTTPRequestHandler):
    server_version = "ccs-mirror"
    protocol_version = "HTTP/1.1"

    # Set from the command line in main().
    allowed = []
    timeout_s = 60
    idbe_dir = None
    idbe_generate = False

    def log_message(self, fmt, *args):
        # The default logger writes every request to stderr unformatted, the
        # request lines are logged with their upstream status in _serve().
        pass

    def _client_allowed(self):
        if not self.allowed:
            return True

        ip = ipaddress.ip_address(self.client_address[0])
        return any(ip in net for net in self.allowed)

    def _reply_empty(self, status):
        self.send_response(status)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _reply_idbe(self, head, body, path, where):
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not head:
            self.wfile.write(body)

        log.info("%s %s %s -> 200 (%d bytes, %s)", self.client_address[0], self.command, path, len(body), where)
        return True

    # An icon the servers of Nintendo no longer have: out of the folder the
    # mirror was started with, else made here. Returns False when there is
    # neither, so the answer stays the one the upstream gave.
    def _serve_idbe_fallback(self, head, name, path):
        if self.idbe_dir is not None:
            local = os.path.join(self.idbe_dir, os.path.basename(name))
            try:
                with open(local, "rb") as f:
                    return self._reply_idbe(head, f.read(), path, "from " + local)
            except OSError:
                pass

        if not self.idbe_generate:
            return False

        # <title ID>.idbe or <title ID>-<version>.idbe, both already checked
        # by the regex that let this path through.
        stem = name[:-len(".idbe")]
        tid, _, version = stem.partition("-")
        try:
            body = idbe.make_idbe(int(tid, 16), version=int(version) if version else 0)
        except (ValueError, struct.error) as e:
            log.error("%s %s %s -> could not make an icon (%s)", self.client_address[0], self.command, path, e)
            return False

        return self._reply_idbe(head, body, path, "made here")

    def _serve(self, head):
        if not self._client_allowed():
            log.warning("%s %s %s -> 403 (client not allowed)", self.client_address[0], self.command, self.path)
            self._reply_empty(403)
            return

        # The console may add a query string, the path alone decides.
        path = self.path.split("?", 1)[0]
        match = PATH_RE.match(path)
        idbe_name = None
        if match is not None:
            url = UPSTREAM + match.group(1).upper() + "/" + match.group(2)
        else:
            match = IDBE_PATH_RE.match(path)
            if match is None:
                log.warning("%s %s %s -> 404 (unknown path)", self.client_address[0], self.command, self.path)
                self._reply_empty(404)
                return

            idbe_name = match.group(2)
            url = IDBE_UPSTREAM + match.group(1) + "/" + idbe_name

        request = urllib.request.Request(url, method="HEAD" if head else "GET")
        request.add_header("User-Agent", USER_AGENT)
        # The console resumes interrupted downloads with a range.
        rng = self.headers.get("Range")
        if rng is not None:
            request.add_header("Range", rng)

        try:
            upstream = urllib.request.urlopen(request, timeout=self.timeout_s)
        except urllib.error.HTTPError as e:
            # The icon of a title Nintendo dropped is what 105-1017 is about:
            # the folder of icons answers where the servers no longer do.
            if idbe_name is not None and self._serve_idbe_fallback(head, idbe_name, path):
                return

            log.info("%s %s %s -> %d (upstream)", self.client_address[0], self.command, path, e.code)
            self._reply_empty(e.code)
            return
        except (urllib.error.URLError, OSError) as e:
            if idbe_name is not None and self._serve_idbe_fallback(head, idbe_name, path):
                return

            log.error("%s %s %s -> 502 (%s)", self.client_address[0], self.command, path, e)
            self._reply_empty(502)
            return

        with upstream:
            status = upstream.status
            self.send_response(status)
            for header in ("Content-Type", "Content-Length", "Content-Range", "Accept-Ranges", "Last-Modified", "ETag"):
                value = upstream.headers.get(header)
                if value is not None:
                    self.send_header(header, value)
            self.end_headers()

            sent = 0
            if not head:
                while True:
                    data = upstream.read(CHUNK)
                    if not data:
                        break
                    self.wfile.write(data)
                    sent += len(data)

        log.info("%s %s %s -> %d (%d bytes)", self.client_address[0], self.command, path, status, sent)

    def do_GET(self):
        self._serve(False)

    def do_HEAD(self):
        self._serve(True)


class ThreadingHTTPSServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        # A handshake that fails (a scanner, a client without TLS 1.0) is one
        # line in the log, not a traceback.
        log.warning("%s: %s", client_address[0], sys.exc_info()[1])


def make_context(cert, key):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    # The Wii U speaks TLS 1.0/1.1 with old ciphers. Security level 0 is what
    # makes current OpenSSL offer them at all.
    with warnings.catch_warnings():
        # Python warns that TLS 1.0 is old, which is the point here.
        warnings.simplefilter("ignore", DeprecationWarning)
        ctx.minimum_version = ssl.TLSVersion.TLSv1
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    ctx.set_ciphers("ALL:@SECLEVEL=0")
    ctx.load_cert_chain(cert, key)
    return ctx


def main():
    parser = argparse.ArgumentParser(description="Mirror of ccs.wup.shop.nintendo.net, answered from the public CDN.")
    parser.add_argument("--cert", required=True, help="cert-chain.pem from SSSL (site certificate + forged CA)")
    parser.add_argument("--key", required=True, help="ssl-cert-private-key.pem from SSSL")
    parser.add_argument("--bind", default="0.0.0.0", help="address to listen on (default: all)")
    parser.add_argument("--port", type=int, default=443, help="port to listen on (default: 443)")
    parser.add_argument("--allow", default="", help="comma separated IPs or networks allowed to connect (default: everyone)")
    parser.add_argument("--timeout", type=int, default=60, help="upstream timeout in seconds (default: 60)")
    parser.add_argument("--idbe-dir", help="folder of .idbe icons to answer with when Nintendo no longer has one (105-1017)")
    parser.add_argument("--idbe-generate", action="store_true", help="make an icon for a title Nintendo has none for, instead of passing the 404 on")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    MirrorHandler.allowed = [ipaddress.ip_network(n.strip(), strict=False) for n in args.allow.split(",") if n.strip()]
    MirrorHandler.timeout_s = args.timeout
    MirrorHandler.idbe_dir = args.idbe_dir
    MirrorHandler.idbe_generate = args.idbe_generate

    try:
        ctx = make_context(args.cert, args.key)
    except ssl.SSLError as e:
        log.error("Could not set up TLS: %s", e)
        return 1

    server = ThreadingHTTPSServer((args.bind, args.port), MirrorHandler)
    # The handshake runs on the first read, in the thread of the request: a
    # slow client cannot hold up the others.
    server.socket = ctx.wrap_socket(server.socket, server_side=True, do_handshake_on_connect=False)
    log.info("Listening on %s:%d, upstream %s and %s, icons from %s, allowed: %s", args.bind, args.port,
             UPSTREAM, IDBE_UPSTREAM, args.idbe_dir or ("made here" if args.idbe_generate else "the servers of Nintendo only"),
             ", ".join(str(n) for n in MirrorHandler.allowed) or "everyone")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
