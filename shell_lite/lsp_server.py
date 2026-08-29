import json
import logging
import re
import sys
from typing import Any, Dict, List, Optional

logging.basicConfig(filename="shell_lite_lsp.log", level=logging.DEBUG, filemode="w")
logger = logging.getLogger("LSP")


class SymbolKind:
    File = 1
    Module = 2
    Namespace = 3
    Package = 4
    Class = 5
    Method = 6
    Property = 7
    Field = 8
    Constructor = 9
    Enum = 10
    Interface = 11
    Function = 12
    Variable = 13
    Constant = 14
    String = 15
    Number = 16
    Boolean = 17
    Array = 18
    Object = 19
    Key = 20
    Null = 21
    EnumMember = 22
    Struct = 23
    Event = 24
    Operator = 25
    TypeParameter = 26


_KEYWORDS = [
    "if", "elif", "else", "while", "until", "unless", "forever",
    "repeat", "times", "for", "in", "to", "give", "say", "print",
    "test", "expect", "ensure", "thing", "has", "can", "use", "import",
    "from", "as", "try", "catch", "always", "error", "spawn", "await",
    "every", "after", "stop", "skip", "yes", "no", "and", "or", "not",
    "is", "be", "more", "less", "than", "equal", "int", "str", "float",
    "bool", "list", "dict", "string", "integer", "decimal", "structure", "class"
]


class Node:
    def __init__(self, line=1, col=1, end_line=1, end_col=1):
        self.line = line
        self.col = col
        self.end_line = end_line
        self.end_col = end_col


class FunctionDef(Node):
    def __init__(self, name, args=None, line=1, col=1, end_line=1, end_col=1):
        super().__init__(line, col, end_line, end_col)
        self.name = name
        self.args = args or []
        self.body = []


class ClassDef(Node):
    def __init__(self, name, line=1, col=1, end_line=1, end_col=1):
        super().__init__(line, col, end_line, end_col)
        self.name = name
        self.body = []


class Assign(Node):
    def __init__(self, name, line=1, col=1, end_line=1, end_col=1):
        super().__init__(line, col, end_line, end_col)
        self.name = name


class TypedAssign(Node):
    def __init__(self, name, type_hint="", line=1, col=1, end_line=1, end_col=1):
        super().__init__(line, col, end_line, end_col)
        self.name = name
        self.type_hint = type_hint


class VarAccess(Node):
    def __init__(self, name, line=1, col=1, end_line=1, end_col=1):
        super().__init__(line, col, end_line, end_col)
        self.name = name


class TopologicalChunk:
    def __init__(self, start_line: int, end_line: int, lines: List[str]):
        self.start_line = start_line
        self.end_line = end_line
        self.lines = lines
        self.symbols: List[dict] = []
        self.diagnostics: List[dict] = []
        self.ast_nodes: List[Node] = []


class ShellLiteDocument:
    def __init__(self, uri: str, text: str):
        self.uri = uri
        self.text = text
        self.lines = text.splitlines(keepends=True)
        self.ast_nodes: List[Node] = []
        self.diagnostics: List[dict] = []
        self.symbols: List[dict] = []
        self.chunks: List[TopologicalChunk] = []
        self.parse_and_analyze()

    def _is_anchor(self, line: str) -> bool:
        if not line or line[0] in (' ', '\t', '\r', '\n', '#'):
            return False
        stripped = line.strip()
        m = re.match(r'^(to|can|structure|thing|class|function|def|namespace|let|make|set|use|import|from|const)\b', stripped)
        return bool(m)

    def update(self, text: str):
        old_lines = self.lines
        new_lines = text.splitlines(keepends=True)
        self.text = text
        self.lines = new_lines

        if not self.chunks or not old_lines or not new_lines:
            self.parse_and_analyze()
            return

        # Find first and last differing lines
        diff_start = 0
        while diff_start < len(old_lines) and diff_start < len(new_lines) and old_lines[diff_start] == new_lines[diff_start]:
            diff_start += 1

        if diff_start == len(old_lines) and len(old_lines) == len(new_lines):
            return

        diff_end_old = len(old_lines) - 1
        diff_end_new = len(new_lines) - 1
        while diff_end_old >= diff_start and diff_end_new >= diff_start and old_lines[diff_end_old] == new_lines[diff_end_new]:
            diff_end_old -= 1
            diff_end_new -= 1

        edit_start_line = diff_start + 1
        edit_end_line = diff_end_old + 1
        line_delta = len(new_lines) - len(old_lines)

        chunk_idx = -1
        for idx, chunk in enumerate(self.chunks):
            if chunk.start_line <= edit_start_line <= chunk.end_line:
                chunk_idx = idx
                break

        if chunk_idx < 0 or edit_end_line > self.chunks[chunk_idx].end_line:
            self.parse_and_analyze()
            return

        target_chunk = self.chunks[chunk_idx]
        new_chunk_end_line = target_chunk.end_line + line_delta
        chunk_lines = new_lines[target_chunk.start_line - 1 : new_chunk_end_line]

        if chunk_idx > 0 and chunk_lines and not self._is_anchor(chunk_lines[0]):
            self.parse_and_analyze()
            return

        symbols, diags, ast_nodes = self._analyze_lines(chunk_lines, target_chunk.start_line)
        target_chunk.end_line = new_chunk_end_line
        target_chunk.lines = chunk_lines
        target_chunk.symbols = symbols
        target_chunk.diagnostics = diags
        target_chunk.ast_nodes = ast_nodes

        for idx in range(chunk_idx + 1, len(self.chunks)):
            c = self.chunks[idx]
            c.start_line += line_delta
            c.end_line += line_delta
            self._shift_chunk_symbols(c, line_delta)

        self._rebuild_from_chunks()

    def _shift_chunk_symbols(self, chunk: TopologicalChunk, delta: int):
        for sym in chunk.symbols:
            r = sym["range"]
            r["start"]["line"] += delta
            r["end"]["line"] += delta
            sr = sym.get("selectionRange")
            if sr:
                sr["start"]["line"] += delta
                sr["end"]["line"] += delta
        for d in chunk.diagnostics:
            r = d["range"]
            r["start"]["line"] += delta
            r["end"]["line"] += delta
        for node in chunk.ast_nodes:
            node.line += delta
            node.end_line += delta

    def _rebuild_from_chunks(self):
        self.symbols = []
        self.diagnostics = []
        self.ast_nodes = []
        for c in self.chunks:
            self.symbols.extend(c.symbols)
            self.diagnostics.extend(c.diagnostics)
            self.ast_nodes.extend(c.ast_nodes)

    def parse_and_analyze(self):
        self.chunks = []
        if not self.lines:
            self.symbols = []
            self.diagnostics = []
            self.ast_nodes = []
            return

        chunk_starts = [0]
        for line_idx in range(1, len(self.lines)):
            if self._is_anchor(self.lines[line_idx]):
                chunk_starts.append(line_idx)

        for i, start_idx in enumerate(chunk_starts):
            end_idx = chunk_starts[i + 1] if i + 1 < len(chunk_starts) else len(self.lines)
            chunk_lines = self.lines[start_idx:end_idx]
            start_line_num = start_idx + 1
            end_line_num = end_idx

            chunk = TopologicalChunk(start_line_num, end_line_num, chunk_lines)
            symbols, diags, ast_nodes = self._analyze_lines(chunk_lines, start_line_num)
            chunk.symbols = symbols
            chunk.diagnostics = diags
            chunk.ast_nodes = ast_nodes
            self.chunks.append(chunk)

        self._rebuild_from_chunks()

    def _analyze_lines(self, lines: List[str], start_line_num: int):
        diagnostics = []
        symbols = []
        ast_nodes = []
        defined_names = {}

        for line_idx, line in enumerate(lines):
            line_num = start_line_num + line_idx
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue

            fn_match = re.match(r'^(?:to|can|function|def)\s+([a-zA-Z_][a-zA-Z0-9_]*)', stripped)
            if fn_match:
                name = fn_match.group(1)
                col = line.find(name) + 1
                node = FunctionDef(name, line=line_num, col=col, end_line=line_num, end_col=col + len(name))
                ast_nodes.append(node)
                symbols.append({
                    "name": name,
                    "kind": SymbolKind.Function,
                    "range": self._node_to_range(node),
                    "selectionRange": self._node_to_range(node),
                    "children": []
                })
                if name in defined_names:
                    diagnostics.append({
                        "range": self._node_to_range(node),
                        "severity": 2,
                        "message": f"Redefinition of function '{name}' (first defined at line {defined_names[name]})"
                    })
                defined_names[name] = line_num
                continue

            cls_match = re.match(r'^(?:structure|thing|class)\s+([a-zA-Z_][a-zA-Z0-9_]*)', stripped)
            if cls_match:
                name = cls_match.group(1)
                col = line.find(name) + 1
                node = ClassDef(name, line=line_num, col=col, end_line=line_num, end_col=col + len(name))
                ast_nodes.append(node)
                symbols.append({
                    "name": name,
                    "kind": SymbolKind.Struct,
                    "range": self._node_to_range(node),
                    "selectionRange": self._node_to_range(node),
                    "children": []
                })
                continue

            assign_match = re.match(r'^(?:make|let|set)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*=', stripped)
            if assign_match:
                name = assign_match.group(1)
                col = line.find(name) + 1
                node = Assign(name, line=line_num, col=col, end_line=line_num, end_col=col + len(name))
                ast_nodes.append(node)
                symbols.append({
                    "name": name,
                    "kind": SymbolKind.Variable,
                    "range": self._node_to_range(node),
                    "selectionRange": self._node_to_range(node),
                })
                continue

        return symbols, diagnostics, ast_nodes

    def _node_to_range(self, node: Node) -> dict:
        return {
            "start": {"line": max(node.line - 1, 0), "character": max(node.col - 1, 0)},
            "end": {"line": max(node.end_line - 1, 0), "character": max(node.end_col - 1, 0)},
        }

    def get_formatting(self) -> List[dict]:
        formatted_lines = []
        indent_level = 0
        for line in self.lines:
            stripped = line.lstrip()
            if not stripped:
                formatted_lines.append("\n")
                continue

            if stripped.startswith(("end", "else", "elif", "catch", "always")):
                indent_level = max(0, indent_level - 1)

            formatted_lines.append("    " * indent_level + stripped)

            if stripped.strip().endswith(":") or stripped.startswith(("if ", "while ", "for ", "to ", "thing ", "test ", "structure ")):
                if not stripped.startswith(("end", "give", "return", "stop", "skip")):
                    indent_level += 1

        new_text = "".join(formatted_lines)
        if new_text == self.text:
            return []

        return [{
            "range": {
                "start": {"line": 0, "character": 0},
                "end": {"line": len(self.lines), "character": 0}
            },
            "newText": new_text
        }]


class LSPServer:
    def __init__(self):
        self._documents: Dict[str, ShellLiteDocument] = {}
        self._running = True

    def _read_message(self, stream) -> Optional[dict]:
        headers = {}
        while True:
            line = stream.readline()
            if not line: return None
            line = line.decode("utf-8").rstrip("\r\n")
            if not line: break
            if ":" in line:
                key, _, val = line.partition(":")
                headers[key.strip().lower()] = val.strip()
        length = int(headers.get("content-length", 0))
        if length == 0: return None
        body = stream.read(length).decode("utf-8")
        return json.loads(body)

    def _write_message(self, stream, payload: dict):
        body = json.dumps(payload).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8")
        stream.write(header + body)
        stream.flush()

    def _notify(self, method: str, params: Any):
        self._write_message(sys.stdout.buffer, {"jsonrpc": "2.0", "method": method, "params": params})

    def _respond(self, req_id: Any, result: Any):
        self._write_message(sys.stdout.buffer, {"jsonrpc": "2.0", "id": req_id, "result": result})

    def _publish_diagnostics(self, doc: ShellLiteDocument):
        self._notify("textDocument/publishDiagnostics", {
            "uri": doc.uri,
            "diagnostics": doc.diagnostics
        })

    def run(self):
        stdin = sys.stdin.buffer
        while self._running:
            try:
                msg = self._read_message(stdin)
                if msg is None: break
                self._handle_message(msg)
            except Exception as e:
                break

    def _handle_message(self, msg: dict):
        method = msg.get("method")
        req_id = msg.get("id")
        params = msg.get("params", {})

        if not method and req_id is not None: return

        if method == "initialize":
            self._respond(req_id, {
                "capabilities": {
                    "textDocumentSync": 1,
                    "hoverProvider": True,
                    "completionProvider": {"triggerCharacters": [" ", "."]},
                    "definitionProvider": True,
                    "documentSymbolProvider": True,
                    "documentFormattingProvider": True,
                    "renameProvider": True,
                    "referencesProvider": True,
                },
                "serverInfo": {"name": "ShellLite Enhanced LSP", "version": "1.0.0"},
            })
        elif method == "textDocument/didOpen":
            uri = params["textDocument"]["uri"]
            text = params["textDocument"]["text"]
            doc = ShellLiteDocument(uri, text)
            self._documents[uri] = doc
            self._publish_diagnostics(doc)
        elif method == "textDocument/didChange":
            uri = params["textDocument"]["uri"]
            text = params["contentChanges"][-1]["text"]
            if uri in self._documents:
                doc = self._documents[uri]
                doc.update(text)
                self._publish_diagnostics(doc)
        elif method == "textDocument/hover":
            self._handle_hover(req_id, params)
        elif method == "textDocument/documentSymbol":
            uri = params["textDocument"]["uri"]
            doc = self._documents.get(uri)
            self._respond(req_id, doc.symbols if doc else [])
        elif method == "textDocument/formatting":
            uri = params["textDocument"]["uri"]
            doc = self._documents.get(uri)
            self._respond(req_id, doc.get_formatting() if doc else [])
        elif method == "textDocument/definition":
            self._handle_definition(req_id, params)
        elif method == "textDocument/completion":
            self._handle_completion(req_id, params)
        elif method == "shutdown":
            self._respond(req_id, None)
        elif method == "exit":
            self._running = False

    def _handle_hover(self, req_id, params):
        uri = params["textDocument"]["uri"]
        pos = params["position"]
        doc = self._documents.get(uri)
        if not doc: return self._respond(req_id, None)

        word = self._get_word_at(doc, pos["line"], pos["character"])
        if word in _KEYWORDS:
            return self._respond(req_id, {"contents": {"kind": "markdown", "value": f"**keyword** `{word}`"}})

        for sym in doc.symbols:
            if sym["name"] == word:
                kind_str = "function" if sym["kind"] == SymbolKind.Function else "variable"
                return self._respond(req_id, {"contents": {"kind": "markdown", "value": f"**{kind_str}** `{word}`"}})

        self._respond(req_id, None)

    def _handle_definition(self, req_id, params):
        uri = params["textDocument"]["uri"]
        pos = params["position"]
        doc = self._documents.get(uri)
        if not doc: return self._respond(req_id, None)

        word = self._get_word_at(doc, pos["line"], pos["character"])
        if not word: return self._respond(req_id, None)

        for sym in doc.symbols:
            if sym["name"] == word:
                return self._respond(req_id, {"uri": uri, "range": sym["range"]})

        self._respond(req_id, None)

    def _handle_completion(self, req_id, params):
        uri = params["textDocument"]["uri"]
        doc = self._documents.get(uri)
        items = []
        seen = set()

        if doc:
            for sym in doc.symbols:
                if sym["name"] not in seen:
                    items.append({"label": sym["name"], "kind": sym["kind"]})
                    seen.add(sym["name"])

        for kw in _KEYWORDS:
            if kw not in seen:
                items.append({"label": kw, "kind": 14})

        self._respond(req_id, {"isIncomplete": False, "items": items})

    def _get_word_at(self, doc: ShellLiteDocument, line: int, char: int) -> str:
        if line >= len(doc.lines): return ""
        text = doc.lines[line]
        start = min(char, len(text))
        while start > 0 and (text[start-1].isalnum() or text[start-1] == '_'):
            start -= 1
        end = min(char, len(text))
        while end < len(text) and (text[end].isalnum() or text[end] == '_'):
            end += 1
        return text[start:end]


def run_lsp():
    server = LSPServer()
    server.run()


if __name__ == "__main__":
    run_lsp()
