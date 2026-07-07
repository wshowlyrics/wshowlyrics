# nix-shell environment for tools/add_furigana.py and tools/add_furigana_ai.py.
# pyopenjtalk provides the context-aware readings, pykakasi the text-preserving
# structure, and jaconv the katakana->hiragana conversion. add_furigana_ai.py
# talks to the cloud AI provider over the standard-library urllib (no extra
# dependency) on top of the same offline engine.
# Usage:
#   nix-shell tools/furigana-shell.nix --run 'python tools/add_furigana.py song.lrc'
#   nix-shell tools/furigana-shell.nix --run 'python tools/add_furigana_ai.py song.lrc'
# Add --regenerate to rebuild readings on already-annotated files:
#   nix-shell tools/furigana-shell.nix --run 'python tools/add_furigana.py --regenerate song.lrc'
{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = [
    (pkgs.python3.withPackages (ps: [ ps.pykakasi ps.pyopenjtalk ps.jaconv ]))
  ];
}
