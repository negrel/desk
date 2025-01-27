{
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, flake-utils, ... }:
    let
      outputsWithoutSystem = { };
      outputsWithSystem = flake-utils.lib.eachDefaultSystem (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          lib = pkgs.lib;
          repo = ./.;
        in
        {
          devShells = {
            default = pkgs.mkShell rec {
              buildInputs = with pkgs; [
                clang-tools
                valgrind-light
                pkg-config

                systemdLibs
                wayland
                wayland-protocols
                wayland-scanner
                wlr-protocols
                pipewire
              ];
              LD_LIBRARY_PATH = "${lib.makeLibraryPath buildInputs}";
              DEBUG = 1;
            };
          };
          packages = {
            powermon = pkgs.callPackage ./src/daemon/powermon/pkg.nix {
              inherit pkgs repo;
            };
          };
        }
      );
    in
    outputsWithSystem // outputsWithoutSystem;
}
