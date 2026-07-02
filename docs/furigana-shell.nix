# nix-shell environment for docs/add_furigana.py (pykakasi).
# Usage:
#   nix-shell docs/furigana-shell.nix --run 'python docs/add_furigana.py song.lrc'
{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = [ (pkgs.python3.withPackages (ps: [ ps.pykakasi ])) ];
}
