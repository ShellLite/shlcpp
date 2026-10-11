import os
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.request
import urllib.error


def _find_exe():
    try:
        from tests.test_runner import get_shl_executable
        return get_shl_executable()
    except FileNotFoundError:
        pass
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for cand in (os.path.join(root, "build-smarty", "shell_lite_exec"),
                 os.path.join(root, "build", "shell_lite_exec")):
        if os.path.exists(cand):
            return cand
    raise unittest.SkipTest("no shlcpp binary built; skipping web tests")


def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


SITE_SCRIPT = """\
to echo_handler(req)
    give "echo:" + req["body"]

to upload_handler(req)
    f = req["files"]["f"]
    give f["filename"] + ":" + f["content"]

std_web_on_request("post", "/echo", echo_handler)
std_web_on_request("post", "/upload", upload_handler)
std_web_listen(%d)
say "listening"
"""


class TestWebModule(unittest.TestCase):
    def setUp(self):
        self.exe = _find_exe()
        self.port = _free_port()
        self.tmp = tempfile.TemporaryDirectory()
        self.script = os.path.join(self.tmp.name, "site.shl")
        with open(self.script, "w", encoding="utf-8") as fh:
            fh.write(SITE_SCRIPT % self.port)
        self.proc = subprocess.Popen(
            [self.exe, self.script],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, cwd=self.tmp.name)
        self.addCleanup(self._cleanup)
        self._wait_for_port()

    def _cleanup(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait(timeout=10)
        self.tmp.cleanup()

    def _wait_for_port(self):
        deadline = time.time() + 15
        while time.time() < deadline:
            s = socket.socket()
            s.settimeout(1)
            try:
                s.connect(("127.0.0.1", self.port))
                s.close()
                return
            except OSError:
                time.sleep(0.2)
            finally:
                s.close()
        self.fail("test server did not start listening")

    def _post(self, path, data, content_type="text/plain"):
        req = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{path}", data=data,
            headers={"Content-Type": content_type}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=10) as res:
                return res.status, res.read().decode("utf-8")
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode("utf-8")

    def test_post_body_reaches_handler(self):
        status, body = self._post("/echo", b"hello-body")
        self.assertEqual(status, 200)
        self.assertEqual(body, "echo:hello-body")

    def test_unmatched_route_is_404(self):
        status, _ = self._post("/nope", b"x")
        self.assertEqual(status, 404)

    def test_multipart_files_exposed(self):
        boundary = "----testboundary42"
        payload = (
            "--" + boundary + "\r\n"
            'Content-Disposition: form-data; name="f"; filename="hi.txt"\r\n'
            "Content-Type: text/plain\r\n\r\n"
            "file-bytes-here\r\n"
            "--" + boundary + "--\r\n"
        ).encode("utf-8")
        status, body = self._post(
            "/upload", payload,
            f"multipart/form-data; boundary={boundary}")
        self.assertEqual(status, 200)
        self.assertEqual(body, "hi.txt:file-bytes-here")

    def test_server_stays_up_after_script_ends(self):
        status, _ = self._post("/echo", b"ping")
        self.assertEqual(status, 200)
        time.sleep(1)
        self.assertIsNone(
            self.proc.poll(),
            "server process exited after the script body finished")

    def test_listen_returns_false_on_bind_failure(self):
        port = _free_port()
        holder = socket.socket()
        holder.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        holder.bind(("127.0.0.1", port))
        holder.listen(1)
        try:
            proc = subprocess.run(
                [self.exe, "-e", f"say std_web_listen({port})"],
                capture_output=True, text=True, timeout=15)
        finally:
            holder.close()
        self.assertEqual(proc.stdout.strip(), "false")
        self.assertIn("could not listen", proc.stderr)

    def test_default_binds_loopback_only(self):
        if not os.path.exists("/proc/net/tcp"):
            raise unittest.SkipTest("no /proc/net/tcp on this platform")
        wanted = f"{self.port:04X}"
        bound_addrs = set()
        with open("/proc/net/tcp", encoding="utf-8") as fh:
            next(fh)
            for line in fh:
                parts = line.split()
                if len(parts) > 3 and parts[3] == "0A":
                    ip_hex, port_hex = parts[1].split(":")
                    if port_hex == wanted:
                        bound_addrs.add(ip_hex)
        self.assertTrue(bound_addrs, "server socket not found")
        self.assertEqual(
            bound_addrs, {"0100007F"},
            f"expected loopback-only bind, got {bound_addrs}")

    def test_listen_rejects_non_number_port(self):
        proc = subprocess.run(
            [self.exe, "-e", 'std_web_listen("nope")'],
            capture_output=True, text=True, timeout=15)
        self.assertIn("web_listen expects port to be a number",
                      proc.stdout + proc.stderr)

    def test_explicit_host_arg(self):
        port = _free_port()
        proc = subprocess.Popen(
            [self.exe, "-e", f'say std_web_listen({port}, "0.0.0.0")'],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            deadline = time.time() + 15
            while time.time() < deadline:
                s = socket.socket()
                s.settimeout(1)
                try:
                    s.connect(("127.0.0.1", port))
                    s.close()
                    break
                except OSError:
                    time.sleep(0.2)
                finally:
                    s.close()
            else:
                self.fail("explicit-host server did not start listening")
            self.assertIsNone(proc.poll(), "explicit-host server exited")
            if os.path.exists("/proc/net/tcp"):
                wanted = f"{port:04X}"
                bound_addrs = set()
                with open("/proc/net/tcp", encoding="utf-8") as fh:
                    next(fh)
                    for line in fh:
                        parts = line.split()
                        if len(parts) > 3 and parts[3] == "0A":
                            ip_hex, port_hex = parts[1].split(":")
                            if port_hex == wanted:
                                bound_addrs.add(ip_hex)
                self.assertIn("00000000", bound_addrs,
                              f"expected wildcard bind, got {bound_addrs}")
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(timeout=10)


if __name__ == "__main__":
    unittest.main()
