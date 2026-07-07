"""Optional cloud-LLM overlay on top of the offline furigana engine.

add_furigana.py already produces high-quality furigana offline (pyopenjtalk
readings over pykakasi structure), which fixes the vast majority of readings for
free and deterministically. This helper adds a cloud LLM on top for the rare
cases a dictionary G2P cannot know — chiefly artful / ateji lyric readings
(運命 sung as さだめ, 永遠 as とわ). It is EXPERIMENTAL: because pyopenjtalk is
usually right and context-aware already, the LLM can also worsen ordinary
readings, so the offline engine is the recommended default. Handy for spot
checks (e.g. comparing a new model) since it reuses the [translation] provider
config, and the model there is tuned for translation, not furigana.

Design (see downstream issue #16):
  * Reuses the [translation] provider config from settings.ini (openai / gemini
    / claude). DeepL / disabled providers cannot emit furigana -> this script
    errors out and points you at add_furigana.py.
  * The file you run is the cost boundary: add_furigana.py = free/offline,
    add_furigana_ai.py = opt-in API cost.
  * Per line, the offline engine (add_furigana.annotate_line) produces the base
    annotation; the LLM output (validated: base text byte-for-byte unchanged,
    every {...} reading pure hiragana) then has its readings merged onto that
    structure. Anything the LLM omits or garbles keeps the offline reading, and
    the numeral-counter policy is re-asserted afterwards.

Usage:
  python add_furigana_ai.py song.lrc [more.srt ...]

Requires an openai/gemini/claude provider + api_key in
  $XDG_CONFIG_HOME/wshowlyrics/settings.ini  (or ~/.config/wshowlyrics/...).
"""

import configparser
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

# Offline engine (pykakasi structure + pyopenjtalk readings + numeral policy)
# plus the shared reconcile/segment helpers. The LLM overlay reuses all of it.
from add_furigana import (
    KANJI_RE,
    PURE_HIRAGANA_RE,
    annotate_line,
    fix_numerals,
    process_file,
    reconcile_readings,
    strip_readings,
)

MAX_RETRIES = 3
HTTP_TIMEOUT = 30  # seconds


# --- Configuration -------------------------------------------------------

def load_translation_config():
    """Read [translation] provider/api_key from settings.ini.

    Returns (config_dict, path) or (None, None) if no settings.ini is found.
    interpolation=None so values containing '%' are not mangled.
    """
    candidates = []
    xdg = os.environ.get('XDG_CONFIG_HOME')
    if xdg:
        candidates.append(os.path.join(xdg, 'wshowlyrics', 'settings.ini'))
    home = os.environ.get('HOME')
    if home:
        candidates.append(os.path.join(home, '.config', 'wshowlyrics', 'settings.ini'))

    for path in candidates:
        if not os.path.isfile(path):
            continue
        parser = configparser.ConfigParser(interpolation=None)
        try:
            parser.read(path, encoding='utf-8')
        except configparser.Error as exc:
            print(f"[add_furigana_ai] failed to parse {path}: {exc}", file=sys.stderr)
            return None, None
        if not parser.has_section('translation'):
            continue
        return {
            'provider': parser.get('translation', 'provider', fallback='').strip(),
            'api_key': parser.get('translation', 'api_key', fallback='').strip(),
        }, path
    return None, None


def provider_kind(provider):
    """Map a provider string to one of the furigana-capable kinds, else None.

    Prefixes mirror the C matchers (openai/gemini/claude _matches()).
    """
    if provider.startswith('gpt-') or provider.startswith('openai'):
        return 'openai'
    if provider.startswith('gemini'):
        return 'gemini'
    if provider.startswith('claude'):
        return 'claude'
    return None


# --- Prompt --------------------------------------------------------------

def build_prompt(line):
    return (
        "You annotate Japanese text with furigana. Add the hiragana reading of "
        "the kanji using EXACTLY the notation BASE{よみ}, where BASE is the "
        "kanji (or kanji compound) and よみ is its hiragana reading. "
        "Examples: 主{ぬし}, 食{た}べる (annotate only the kanji part of "
        "okurigana).\n"
        "Rules:\n"
        "- Read the whole line as one unit and choose the reading that fits "
        "the sentence's meaning (e.g. 君 as きみ not くん; 今日 as きょう not "
        "こんにち when it means 'today').\n"
        "- Keep a compound word that has a single non-compositional reading "
        "(jukujikun) as ONE unit spanning all its kanji: 今日{きょう}, "
        "明日{あした}, 一人{ひとり}, 二人{ふたり}, 大人{おとな}. NEVER split "
        "such a word per character (do NOT write 今{こん}日{にち}).\n"
        "- Give EVERY kanji a reading; never leave a kanji un-annotated "
        "(好き must be 好{す}き, not 好き).\n"
        "- Only annotate kanji. Leave hiragana, katakana, punctuation and "
        "numbers untouched.\n"
        "- Keep every character of the original EXACTLY as given. Do not "
        "translate, rephrase, correct, reorder, or add/remove any character "
        "outside the {} readings.\n"
        "- If a segment already has a reading like 主{ふり}, keep it unchanged.\n"
        "- Readings inside {} must be pure hiragana.\n"
        "- Output ONLY the single annotated line, nothing else.\n\n"
        f"Line: {line}"
    )


# --- Provider HTTP calls (mirror the C build_request_json/parse) ---------

def _post_json(url, headers, body):
    data = json.dumps(body).encode('utf-8')
    req = urllib.request.Request(url, data=data, headers=headers, method='POST')
    with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
        return json.loads(resp.read().decode('utf-8'))


def _call_once(kind, provider, api_key, prompt):
    """One HTTP round-trip. Returns the model's raw text or None."""
    if kind == 'openai':
        body = {
            "model": provider,
            "messages": [{"role": "user", "content": prompt}],
            "temperature": 0,
        }
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        }
        resp = _post_json("https://api.openai.com/v1/chat/completions", headers, body)
        return resp["choices"][0]["message"]["content"]

    if kind == 'claude':
        body = {
            "model": provider,
            "max_tokens": 1024,
            "temperature": 0,
            "messages": [{"role": "user", "content": prompt}],
        }
        headers = {
            "Content-Type": "application/json",
            "x-api-key": api_key,
            "anthropic-version": "2023-06-01",
        }
        resp = _post_json("https://api.anthropic.com/v1/messages", headers, body)
        return resp["content"][0]["text"]

    if kind == 'gemini':
        body = {
            "contents": [{"parts": [{"text": prompt}]}],
            "generationConfig": {"temperature": 0},
        }
        headers = {"Content-Type": "application/json"}
        url = (
            "https://generativelanguage.googleapis.com/v1beta/models/"
            f"{provider}:generateContent?key={api_key}"
        )
        resp = _post_json(url, headers, body)
        return resp["candidates"][0]["content"]["parts"][0]["text"]

    return None


def call_llm(cfg, prompt):
    """Call the provider with retry/backoff. Returns raw text or None."""
    kind = cfg['_kind']
    for attempt in range(MAX_RETRIES):
        try:
            return _call_once(kind, cfg['provider'], cfg['api_key'], prompt)
        except (urllib.error.URLError, KeyError, IndexError, ValueError, OSError) as exc:
            if attempt == MAX_RETRIES - 1:
                print(f"[add_furigana_ai] provider call failed: {exc}", file=sys.stderr)
                return None
            time.sleep(2 ** attempt)
    return None


# --- Response handling ---------------------------------------------------

def last_nonempty_line(text):
    """Mirror translator_extract_last_line: guard against AI over-explanation."""
    if text is None:
        return None
    lines = [ln for ln in text.strip().splitlines() if ln.strip()]
    return lines[-1] if lines else None


def is_valid(candidate, original):
    """Hybrid-policy validation: reject hallucinated edits / non-hiragana."""
    if not candidate:
        return False
    # 1. Base text must be byte-for-byte identical (both sides stripped of
    #    readings, so a pre-existing 主{ふり} in the original is handled).
    if strip_readings(candidate) != strip_readings(original):
        return False
    # 2. Every reading must be pure hiragana.
    readings = re.findall(r'\{([^}]*)\}', candidate)
    return all(PURE_HIRAGANA_RE.match(r) for r in readings)


def make_ai_annotate(cfg):
    """Build the per-line annotator: the offline engine, then LLM readings on top.

    The LLM can override readings on standard kanji runs — useful for the artful
    / ateji readings a dictionary lacks — but it may also worsen ordinary
    readings, so this overlay is experimental (see the module docstring). The
    numeral-counter policy is always re-asserted after the LLM.
    """
    def ai_annotate(text):
        if not KANJI_RE.search(text):
            return text
        base = annotate_line(text)          # pykakasi + pyopenjtalk + numerals
        candidate = last_nonempty_line(call_llm(cfg, build_prompt(text)))
        if not is_valid(candidate, text):
            return base                     # no usable LLM output
        merged = reconcile_readings(base, candidate)
        return fix_numerals(merged)         # re-assert numeral policy over LLM
    return ai_annotate


# --- CLI -----------------------------------------------------------------

def main(argv):
    regenerate = "--regenerate" in argv
    files = [a for a in argv[1:] if not a.startswith("-")]
    if not files:
        print("Usage: python add_furigana_ai.py [--regenerate] <file...>",
              file=sys.stderr)
        return 1

    cfg, path = load_translation_config()
    if cfg is None:
        print("[add_furigana_ai] settings.ini not found (looked in "
              "$XDG_CONFIG_HOME/wshowlyrics and ~/.config/wshowlyrics).",
              file=sys.stderr)
        return 2

    kind = provider_kind(cfg['provider'])
    if kind is None:
        print(f"[add_furigana_ai] provider='{cfg['provider'] or '(unset)'}' in "
              f"{path} cannot generate furigana. Use an openai/gemini/claude "
              "provider, or run add_furigana.py for offline pykakasi.",
              file=sys.stderr)
        return 2
    if not cfg['api_key']:
        print(f"[add_furigana_ai] api_key is empty in {path}.", file=sys.stderr)
        return 2

    cfg['_kind'] = kind
    print(f"[add_furigana_ai] using provider '{cfg['provider']}' "
          f"(offline pyopenjtalk base, LLM overlay).", file=sys.stderr)
    annotate = make_ai_annotate(cfg)
    for filepath in files:
        process_file(filepath, annotate_fn=annotate, regenerate=regenerate)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
