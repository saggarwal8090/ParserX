"""
TinyLang — Flask Web Compiler
This file acts as a thin Python Flask subprocess wrapper.
All core compiler logic (Lexer, Parser, AST, Optimizer) lives in the C executable.
This backend receives HTTP requests containing source code, runs the C executable, and returns the JSON output.
"""

import os
import sys
import json
import time
import subprocess
import tempfile

from flask import Flask, render_template, request, jsonify
from compiler.grammar_tool import analyze_grammar

# Initialize the Flask web application instance
app = Flask(__name__)

# ── Helper Function: Locate the C compiler binary ───────────────────────────────
def _find_compiler():
    """
    Search for the compiled C executable ('tinylang_compiler' or 'tinylang_compiler.exe').
    Returns the absolute path to the binary if found, otherwise returns None.
    """
    # Get the directory where this app.py file is located
    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    # On Windows, we look for the .exe extension
    if sys.platform == 'win32':
        candidates = [os.path.join(base_dir, 'tinylang_compiler.exe')]
    # On Mac/Linux, we look for the file without extension first, then .exe as fallback
    else:
        candidates = [
            os.path.join(base_dir, 'tinylang_compiler'),
            os.path.join(base_dir, 'tinylang_compiler.exe'),
        ]
        
    # Iterate through our potential binary paths
    for c in candidates:
        if os.path.isfile(c): # Check if the file actually exists on disk
            return c
            
    # Return None if the executable was not found
    return None


# ── Route: Main Page ────────────────────────────────────────────────────────────
@app.route('/')
def index():
    """
    Serve the main HTML interface (templates/index.html) when a user visits the root URL (/).
    """
    return render_template('index.html')


# ── Route: Compile Code ─────────────────────────────────────────────────────────
@app.route('/compile', methods=['POST'])
def compile_code():
    """
    Endpoint that receives source code from the frontend, writes it to a temp file,
    executes the C compiler, and relays the JSON result back to the frontend.
    """
    # Attempt to parse the incoming JSON payload (silent=True ignores parsing errors)
    data = request.get_json(silent=True) or {}

    # Debug log to terminal: print the keys received in the JSON payload
    print(f"[DEBUG] /compile received keys: {list(data.keys())}", file=sys.stderr)

    # Extract the code block. Support 'code' or 'source_code' keys for flexibility.
    source_code = data.get('code', data.get('source_code', ''))

    # Debug logs: print code length and check if it's empty
    print(f"[DEBUG] source_code length : {len(source_code)}", file=sys.stderr)
    print(f"[DEBUG] source_code empty  : {not source_code.strip()}", file=sys.stderr)
    
    # If code exists, log the first 200 characters for debugging purposes
    if source_code:
        preview = source_code[:200].replace('\n', '\\n').replace('\r', '\\r')
        print(f"[DEBUG] source_code preview: {preview}", file=sys.stderr)

    # Locate the path to our C compiler binary
    compiler_path = _find_compiler()
    
    # If the binary is missing, attempt to build it automatically
    if not compiler_path:
        print("[DEBUG] Compiler missing, attempting to build with 'build_compiler.py'...", file=sys.stderr)
        try:
            # Execute the 'build_compiler.py' script using the current Python interpreter
            subprocess.run([sys.executable, "build_compiler.py"], check=True, capture_output=True)
            # Re-check for the compiler binary after building
            compiler_path = _find_compiler()
        except Exception as e:
            # If the build process crashes, capture and log the specific error details
            error_detail = str(e)
            if hasattr(e, 'stderr') and e.stderr:
                error_detail += f"\nStderr: {e.stderr.decode()}"
            print(f"[DEBUG] Build failed: {error_detail}", file=sys.stderr)

    # If the compiler binary is still missing (build failed or no GCC), return a JSON error response
    if not compiler_path:
        return jsonify({
            'status': 'error',
            'error': 'Compiler binary not found and build failed. Vercel environment might lack GCC. Consider a platform with persistent storage or full system access.',
            'tokens': [], 'parse_table': {}, 'ast': {},
            'symbol_table': [], 'optimizer': {}, 'compile_time_ms': 0,
        })

    # If the user submitted empty code, return an error immediately without invoking the compiler
    if not source_code.strip():
        return jsonify({
            'status': 'error',
            'error': 'No source code provided.',
            'tokens': [], 'parse_table': {}, 'ast': {},
            'symbol_table': [], 'optimizer': {}, 'compile_time_ms': 0,
        })

    # Record the start time to calculate total compilation duration
    start = time.time()

    # ── Write source to a temp file and pass path as argv[1] ──
    # We write the code to a temporary file instead of passing via stdin to avoid deadlocks
    # and to sidestep newline corruption (\r\n) issues, especially on Windows.
    tmp_file = None
    try:
        # Create a named temporary file with a '.tl' extension
        with tempfile.NamedTemporaryFile(
                mode='w', suffix='.tl', delete=False, encoding='utf-8') as f:
            f.write(source_code) # Write the user's source code into the file
            tmp_file = f.name    # Save the file's absolute path

        print(f"[DEBUG] temp file          : {tmp_file}", file=sys.stderr)

        try:
            # Execute the C compiler as a subprocess, passing the temporary file path as an argument
            proc = subprocess.run(
                [compiler_path, tmp_file],
                capture_output=True, # Capture stdout (JSON output) and stderr (errors)
                text=True,           # Treat outputs as strings, not bytes
                timeout=10,          # Kill the compiler if it takes more than 10 seconds (e.g. infinite loops)
                encoding='utf-8',
                errors='replace',
            )
        except FileNotFoundError:
            # Handle edge case where OS fails to launch the binary
            return jsonify({
                'status': 'error',
                'error': 'Compiler not found. Run `make` (or build.bat) in the project root first.',
                'tokens': [], 'parse_table': {}, 'ast': {},
                'symbol_table': [], 'optimizer': {}, 'compile_time_ms': 0,
            })
        except subprocess.TimeoutExpired:
            # Handle edge case where the compiler hangs or takes too long
            return jsonify({
                'status': 'error',
                'error': 'Compilation timed out.',
                'tokens': [], 'parse_table': {}, 'ast': {},
                'symbol_table': [], 'optimizer': {}, 'compile_time_ms': 10000,
            })

    finally:
        # Guarantee that the temporary file is deleted from the OS regardless of success or failure
        if tmp_file and os.path.exists(tmp_file):
            try:
                os.unlink(tmp_file) # Remove the file
            except OSError:
                pass # Ignore deletion errors

    # Calculate compilation time in milliseconds
    elapsed_ms = round((time.time() - start) * 1000, 2)

    # ── Debug: log subprocess results ────────────────────────
    print(f"[DEBUG] proc.returncode    : {proc.returncode}", file=sys.stderr)
    if proc.stderr:
        print(f"[DEBUG] proc.stderr        : {proc.stderr[:300]}", file=sys.stderr)
    print(f"[DEBUG] proc.stdout (500ch): {proc.stdout[:500]}", file=sys.stderr)

    # If the C executable returned a non-zero exit code (indicating a compilation error)
    if proc.returncode != 0:
        # Fallback error message based on stderr
        stderr_msg = (proc.stderr or '').strip() or 'Unknown compiler error.'
        try:
            # Try to see if the compiler printed structured JSON error data to stdout
            err_json = json.loads(proc.stdout)
            error_msg = err_json.get('error', stderr_msg)
        except (json.JSONDecodeError, ValueError):
            # If stdout isn't valid JSON, fallback to raw stderr
            error_msg = stderr_msg
            
        # Return the error response to the frontend
        return jsonify({
            'status': 'error',
            'error': error_msg,
            'tokens': [], 'parse_table': {}, 'ast': {},
            'symbol_table': [], 'optimizer': {}, 'compile_time_ms': elapsed_ms,
        })

    # Parse the successful JSON payload printed by the C compiler to stdout
    try:
        result = json.loads(proc.stdout)
    except (json.JSONDecodeError, ValueError):
        # If the compiler succeeded but output invalid JSON (e.g., segfault prints or printf pollution)
        print(f"[DEBUG] JSON parse failed on stdout (first 1000 chars): {proc.stdout[:1000]}", file=sys.stderr)
        return jsonify({
            'status': 'error',
            'error': 'Compiler returned malformed output.',
            'tokens': [], 'parse_table': {}, 'ast': {},
            'symbol_table': [], 'optimizer': {}, 'compile_time_ms': elapsed_ms,
        })

    # Ensure all required frontend keys exist in the dictionary, providing empty defaults if missing
    result.setdefault('tokens',       [])
    result.setdefault('parse_table',  {})
    result.setdefault('ast',          {})
    result.setdefault('symbol_table', [])
    result.setdefault('optimizer',    {})
    result.setdefault('status',       'success')
    result.setdefault('error',        '')
    
    # Attach the total compile time measured by Python
    result['compile_time_ms'] = elapsed_ms

    # Return the fully parsed and enriched JSON response back to the client
    return jsonify(result)


# ── Route: Grammar Analyzer (Python Tool) ───────────────────────────────────────
@app.route('/grammar-analyze', methods=['POST'])
def grammar_analyze():
    """
    Analyze a user-provided arbitrary grammar string.
    This feature is kept in Python using 'compiler/grammar_tool.py' so it can compute
    FIRST/FOLLOW sets, parse tables, and conflicts on *any* grammar provided by the user.
    """
    # Extract the payload from the HTTP POST request
    data = request.get_json(silent=True) or {}
    grammar_text  = data.get('grammar', '')
    input_string  = data.get('input_string', '')

    # Reject empty grammars
    if not grammar_text.strip():
        return jsonify({'status': 'error', 'error': 'No grammar provided.'})

    try:
        # Call the Python tool to compute FIRST/FOLLOW, conflicts, and LL(1) Table
        result = analyze_grammar(grammar_text, input_string or None)
        # Return the analysis JSON to the client
        return jsonify(result)
    except Exception as e:
        # Handle and format any arbitrary parsing errors raised by the grammar_tool
        return jsonify({'status': 'error', 'error': f'Analysis error: {str(e)}'})


# ── Application Entry Point ─────────────────────────────────────────────────────
if __name__ == '__main__':
    # Start the Flask development server on port 5000 with auto-reload (debug) enabled
    app.run(debug=True, port=5000)

