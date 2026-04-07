"""
TinyLang Grammar Tool — Generic LL(1) analysis engine.
Parses user-provided grammars, computes FIRST/FOLLOW sets,
builds LL(1) parse tables, detects conflicts, and runs stack-based parsing.
"""


# ────────────────────────────────────────────
# Grammar Text Parser
# ────────────────────────────────────────────

def parse_grammar_text(text: str) -> dict:
    """
    Parse a textual grammar definition into internal representation.

    Input format (one rule per line):
        E  -> T E'
        E' -> + T E' | ε
        T  -> F T'
        T' -> * F T' | ε
        F  -> ( E ) | id

    Returns: dict { NonTerminal: [ [sym1, sym2, ...], [sym3, ...], ... ] }
    """
    grammar = {}
    lines = text.strip().split('\n')

    for line in lines:
        line = line.strip()
        if not line or line.startswith('//') or line.startswith('#'):
            continue

        # Support both -> and →
        if '→' in line:
            lhs, rhs = line.split('→', 1)
        elif '->' in line:
            lhs, rhs = line.split('->', 1)
        else:
            continue

        lhs = lhs.strip()
        if not lhs:
            continue

        # Split alternatives by |
        alternatives = rhs.split('|')
        for alt in alternatives:
            symbols = alt.strip().split()
            if not symbols:
                symbols = ['ε']
            # Normalize epsilon representations
            symbols = ['ε' if s in ('ε', 'epsilon', 'eps', "''", '""', 'λ', 'ε') else s for s in symbols]
            if lhs not in grammar:
                grammar[lhs] = []
            grammar[lhs].append(symbols)

    return grammar


# ────────────────────────────────────────────
# Identify Non-terminals and Terminals
# ────────────────────────────────────────────

def get_non_terminals(grammar: dict) -> list:
    """Get sorted list of non-terminals (keys of the grammar)."""
    return list(grammar.keys())


def get_terminals(grammar: dict) -> list:
    """Get sorted list of terminals (symbols that are not non-terminals and not ε)."""
    non_terminals = set(grammar.keys())
    terminals = set()
    for alts in grammar.values():
        for alt in alts:
            for sym in alt:
                if sym not in non_terminals and sym != 'ε':
                    terminals.add(sym)
    return sorted(terminals)


# ────────────────────────────────────────────
# Left Recursion Detection
# ────────────────────────────────────────────

def check_left_recursion(grammar: dict) -> list:
    """
    Detect direct left recursion in the grammar.
    Returns list of warning dicts.
    """
    warnings = []
    for nt, alternatives in grammar.items():
        for alt in alternatives:
            if alt and alt[0] == nt:
                prod_str = f"{nt} → {' '.join(alt)}"
                warnings.append({
                    'nonterminal': nt,
                    'production': prod_str,
                    'message': (
                        f"Direct left recursion detected: {prod_str}. "
                        f"LL(1) parsers cannot handle left-recursive grammars. "
                        f"Consider rewriting using left factoring."
                    )
                })
    return warnings


# ────────────────────────────────────────────
# FIRST Sets
# ────────────────────────────────────────────

def compute_first(grammar: dict) -> dict:
    """
    Compute FIRST sets for all non-terminals using fixed-point iteration.
    Returns { NonTerminal: set_of_terminals_plus_epsilon }.
    """
    non_terminals = set(grammar.keys())
    first = {nt: set() for nt in grammar}

    changed = True
    while changed:
        changed = False
        for nt, alternatives in grammar.items():
            for alt in alternatives:
                # Handle epsilon production
                if alt == ['ε']:
                    if 'ε' not in first[nt]:
                        first[nt].add('ε')
                        changed = True
                    continue

                # Walk through symbols in the alternative
                all_can_be_empty = True
                for sym in alt:
                    if sym in non_terminals:
                        # Add FIRST(sym) - {ε} to FIRST(nt)
                        before = len(first[nt])
                        first[nt] |= (first[sym] - {'ε'})
                        if len(first[nt]) != before:
                            changed = True
                        # If sym cannot derive ε, stop here
                        if 'ε' not in first[sym]:
                            all_can_be_empty = False
                            break
                    else:
                        # Terminal symbol
                        if sym not in first[nt]:
                            first[nt].add(sym)
                            changed = True
                        all_can_be_empty = False
                        break

                # If all symbols in the alternative can derive ε
                if all_can_be_empty:
                    if 'ε' not in first[nt]:
                        first[nt].add('ε')
                        changed = True

    return first


def first_of_sequence(seq: list, first_sets: dict, non_terminals: set) -> set:
    """Compute FIRST of a sequence of grammar symbols."""
    result = set()
    for sym in seq:
        if sym == 'ε':
            continue
        if sym in non_terminals:
            result |= (first_sets[sym] - {'ε'})
            if 'ε' not in first_sets[sym]:
                return result
        else:
            # Terminal
            result.add(sym)
            return result
    # All symbols can derive ε
    result.add('ε')
    return result


# ────────────────────────────────────────────
# FOLLOW Sets
# ────────────────────────────────────────────

def compute_follow(grammar: dict, first_sets: dict, start_symbol: str = None) -> dict:
    """
    Compute FOLLOW sets for all non-terminals using fixed-point iteration.
    Automatically uses the first non-terminal as start symbol if not specified.
    """
    non_terminals = set(grammar.keys())
    follow = {nt: set() for nt in grammar}

    # Determine start symbol
    if start_symbol is None:
        start_symbol = list(grammar.keys())[0]
    follow[start_symbol].add('$')

    changed = True
    while changed:
        changed = False
        for nt, alternatives in grammar.items():
            for alt in alternatives:
                if alt == ['ε']:
                    continue
                for idx, sym in enumerate(alt):
                    if sym not in non_terminals:
                        continue

                    # β = everything after sym in this production
                    beta = alt[idx + 1:]

                    if beta:
                        first_beta = first_of_sequence(beta, first_sets, non_terminals)
                        before = len(follow[sym])
                        follow[sym] |= (first_beta - {'ε'})
                        # If β can derive ε, add FOLLOW(nt) to FOLLOW(sym)
                        if 'ε' in first_beta:
                            follow[sym] |= follow[nt]
                        if len(follow[sym]) != before:
                            changed = True
                    else:
                        # sym is at the end of the production
                        before = len(follow[sym])
                        follow[sym] |= follow[nt]
                        if len(follow[sym]) != before:
                            changed = True

    return follow


# ────────────────────────────────────────────
# LL(1) Parse Table
# ────────────────────────────────────────────

def build_parse_table(grammar: dict, first_sets: dict, follow_sets: dict) -> tuple:
    """
    Build LL(1) parse table and detect conflicts.

    Returns:
        table: dict { NonTerminal: { terminal: production_string } }
              For conflicting cells (non-LL(1) grammar) the value contains
              ALL competing productions joined by " / " so the frontend can
              display every entry in the cell.
        conflicts: list of conflict dicts
    """
    non_terminals = set(grammar.keys())
    # Use a dict-of-dict-of-lists to detect conflicts
    table_entries = {nt: {} for nt in grammar}
    conflicts = []

    for nt, alternatives in grammar.items():
        for alt in alternatives:
            prod_str = f"{nt} → {' '.join(alt)}"
            first_alt = first_of_sequence(alt, first_sets, non_terminals)

            # For each terminal a in FIRST(α), add to M[A, a]
            for terminal in first_alt:
                if terminal != 'ε':
                    if terminal not in table_entries[nt]:
                        table_entries[nt][terminal] = []
                    table_entries[nt][terminal].append(prod_str)

            # If ε is in FIRST(α), for each terminal b in FOLLOW(A), add to M[A, b]
            if 'ε' in first_alt:
                for terminal in follow_sets[nt]:
                    if terminal not in table_entries[nt]:
                        table_entries[nt][terminal] = []
                    table_entries[nt][terminal].append(prod_str)

    # Build final table and detect conflicts
    table = {nt: {} for nt in grammar}
    for nt in grammar:
        for terminal, rules in table_entries[nt].items():
            if len(rules) > 1:
                # Conflict — multiple rules for same (nt, terminal)
                conflicts.append({
                    'nonterminal': nt,
                    'terminal': terminal,
                    'rules': rules,
                    'message': (
                        f"LL(1) Conflict at M[{nt}, {terminal}]: "
                        f"multiple productions {', '.join(rules)}. "
                        f"This grammar is NOT LL(1)."
                    )
                })
                # Store ALL competing productions joined by " / " so the
                # frontend can show every entry in the conflicting cell.
                table[nt][terminal] = ' / '.join(rules)
            else:
                table[nt][terminal] = rules[0]

    return table, conflicts


# ────────────────────────────────────────────
# Stack-based LL(1) Predictive Parser
# ────────────────────────────────────────────

def parse_input_string(input_str: str, parse_table: dict, start_symbol: str,
                       non_terminals: set, max_steps: int = 200) -> dict:
    """
    Run stack-based LL(1) predictive parsing on a space-separated input string.

    Args:
        input_str: space-separated tokens, e.g. "id + id * id"
        parse_table: M[A, a] table
        start_symbol: start non-terminal
        non_terminals: set of non-terminals
        max_steps: safety limit

    Returns dict with parse_steps, status, error_message.
    """
    # Tokenize the input string (space-separated)
    input_tokens = input_str.strip().split()
    input_tokens.append('$')  # End marker

    stack = ['$', start_symbol]
    pos = 0
    steps = []
    step_num = 0

    while stack and step_num < max_steps:
        step_num += 1
        top = stack[-1]
        current = input_tokens[pos] if pos < len(input_tokens) else '$'

        # Build remaining input display
        remaining = ' '.join(input_tokens[pos:])
        if len(remaining) > 40:
            remaining = remaining[:37] + '...'

        stack_display = ' '.join(reversed(stack))

        # Accept condition
        if top == '$':
            if current == '$':
                steps.append({
                    'step': step_num,
                    'stack': stack_display,
                    'input': remaining,
                    'action': 'Accept ✅'
                })
                return {
                    'parse_steps': steps,
                    'status': 'accepted',
                    'error_message': ''
                }
            else:
                steps.append({
                    'step': step_num,
                    'stack': stack_display,
                    'input': remaining,
                    'action': f"Error: unexpected '{current}'"
                })
                return {
                    'parse_steps': steps,
                    'status': 'error',
                    'error_message': f"Unexpected token '{current}' — expected end of input"
                }

        # Terminal on top of stack → match
        if top not in non_terminals:
            if top == current:
                steps.append({
                    'step': step_num,
                    'stack': stack_display,
                    'input': remaining,
                    'action': f"Match '{top}'"
                })
                stack.pop()
                pos += 1
            else:
                steps.append({
                    'step': step_num,
                    'stack': stack_display,
                    'input': remaining,
                    'action': f"Error: expected '{top}', got '{current}'"
                })
                return {
                    'parse_steps': steps,
                    'status': 'error',
                    'error_message': f"Expected '{top}' but got '{current}'"
                }
        else:
            # Non-terminal on top → expand using parse table
            if current in parse_table.get(top, {}):
                production = parse_table[top][current]
                steps.append({
                    'step': step_num,
                    'stack': stack_display,
                    'input': remaining,
                    'action': f"Apply: {production}"
                })
                stack.pop()
                # Extract RHS symbols
                rhs_str = production.split('→')[1].strip()
                rhs = rhs_str.split()
                if rhs != ['ε']:
                    for sym in reversed(rhs):
                        stack.append(sym)
            else:
                steps.append({
                    'step': step_num,
                    'stack': stack_display,
                    'input': remaining,
                    'action': f"Error: no rule for M[{top}, {current}]"
                })
                return {
                    'parse_steps': steps,
                    'status': 'error',
                    'error_message': f"No parse table entry for M[{top}, '{current}']"
                }

    if step_num >= max_steps:
        return {
            'parse_steps': steps,
            'status': 'error',
            'error_message': f'Parsing exceeded {max_steps} steps'
        }

    return {
        'parse_steps': steps,
        'status': 'accepted',
        'error_message': ''
    }


# ────────────────────────────────────────────
# Public API — Full Analysis Pipeline
# ────────────────────────────────────────────

def analyze_grammar(grammar_text: str, input_string: str = None) -> dict:
    """
    Run the full LL(1) analysis pipeline on a user-provided grammar.

    Args:
        grammar_text: textual grammar (one rule per line)
        input_string: optional space-separated input to parse

    Returns dict with all analysis results (JSON-serializable).
    """
    # 1. Parse the grammar text
    grammar = parse_grammar_text(grammar_text)

    if not grammar:
        return {
            'status': 'error',
            'error': 'Could not parse grammar. Use format: A -> B c | d',
            'grammar_parsed': {},
            'non_terminals': [],
            'terminals': [],
            'first_sets': {},
            'follow_sets': {},
            'parse_table': {},
            'conflicts': [],
            'is_ll1': False,
            'left_recursion_warnings': [],
            'parse_steps': [],
            'parse_status': '',
            'parse_error': ''
        }

    non_terminals = get_non_terminals(grammar)
    terminals = get_terminals(grammar)
    start_symbol = non_terminals[0]

    # 2. Check for left recursion
    lr_warnings = check_left_recursion(grammar)

    # 3. Compute FIRST sets
    first_sets = compute_first(grammar)

    # 4. Compute FOLLOW sets
    follow_sets = compute_follow(grammar, first_sets, start_symbol)

    # 5. Build parse table & detect conflicts
    parse_table, conflicts = build_parse_table(grammar, first_sets, follow_sets)

    is_ll1 = len(conflicts) == 0 and len(lr_warnings) == 0

    # 6. Format grammar for display
    grammar_display = {}
    for nt, alts in grammar.items():
        grammar_display[nt] = [' '.join(alt) for alt in alts]

    # 7. Convert sets to sorted lists for JSON
    first_json = {nt: sorted(s) for nt, s in first_sets.items()}
    follow_json = {nt: sorted(s) for nt, s in follow_sets.items()}

    result = {
        'status': 'success',
        'error': '',
        'grammar_parsed': grammar_display,
        'non_terminals': non_terminals,
        'terminals': terminals,
        'start_symbol': start_symbol,
        'first_sets': first_json,
        'follow_sets': follow_json,
        'parse_table': parse_table,
        'conflicts': conflicts,
        'is_ll1': is_ll1,
        'left_recursion_warnings': lr_warnings,
        'parse_steps': [],
        'parse_status': '',
        'parse_error': ''
    }

    # 8. Parse input string if provided
    if input_string and input_string.strip():
        nt_set = set(non_terminals)
        parse_result = parse_input_string(input_string, parse_table, start_symbol, nt_set)
        result['parse_steps'] = parse_result['parse_steps']
        result['parse_status'] = parse_result['status']
        result['parse_error'] = parse_result['error_message']

    return result
