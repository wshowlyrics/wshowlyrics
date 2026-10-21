# SPDX-License-Identifier: GPL-3.0-or-later
# nix-shell environment for tools/tag_youtube_media.py and tools/link_spotify_lyrics.py.
# yt-dlp reads YouTube's metadata for the video ID in the file name,
# mkvtoolnix-cli (mkvextract/mkvpropedit) reads and rewrites the Matroska tags.
# The iTunes Search and Spotify Web APIs are reached over the standard-library urllib.
# Usage:
#   nix-shell tools/youtube-tag-shell.nix --run 'python tools/tag_youtube_media.py --dry-run ~/Videos/Youtube'
#   nix-shell tools/youtube-tag-shell.nix --run 'python tools/link_spotify_lyrics.py --dry-run ~/Videos/Youtube'
{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = [ pkgs.python3 pkgs.yt-dlp pkgs.mkvtoolnix-cli ];
}
