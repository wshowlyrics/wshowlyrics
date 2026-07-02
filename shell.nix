# Development shell for building wshowlyrics on NixOS.
#
#   nix-shell                 # enter the dev shell
#   meson setup build && meson compile -C build
#
# Fuzz targets need clang (libFuzzer + ASan); valgrind build disables ASan:
#   CC=clang meson setup build-fuzz -Dfuzzing=true
#   CC=clang meson setup build-valgrind -Dfuzzing=true -Dfuzz_sanitizer=none
{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  # nixpkgs injects -D_FORTIFY_SOURCE=2, which warns under the default debug
  # (-O0) build; the project's werror=true turns that warning into an error.
  hardeningDisable = [ "fortify" ];
  nativeBuildInputs = with pkgs; [
    meson
    ninja
    pkg-config
    wayland-scanner  # generates protocol glue (protocols/meson.build)
    clang     # fuzz targets build with CC=clang (libFuzzer + ASan)
    valgrind  # valgrind-compatible fuzz build
  ];
  buildInputs = with pkgs; [
    cairo
    pango
    fontconfig
    wayland
    wayland-protocols
    curl
    openssl
    json_c
    libappindicator-gtk3
    gdk-pixbuf
    glib                 # provides gio-2.0
    libexttextcat        # optional: language detection (graceful degrade)
  ];
}
