{
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

    ghostty = {
      url = "github:negrel/ghostty";
    };
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
          buildInputs = with pkgs; [
            clang
            pkg-config

            systemdLibs
            wayland
            wayland-protocols
            wayland-scanner
            wlr-protocols
            pipewire
          ];
        in
        {
          devShells = {
            default = pkgs.mkShell {
              buildInputs =
                with pkgs;
                [
                  clang-tools
                  valgrind-light
                  gopls
                ]
                ++ buildInputs;
              LD_LIBRARY_PATH = "${lib.makeLibraryPath buildInputs}";
              DEBUG = 1;
            };
          };
          packages = {
            default = pkgs.stdenv.mkDerivation {
              name = "desk";
              version = "0.1.0";

              buildInputs = buildInputs;

              src = repo;
              PROJECT_DIR = repo;

              buildPhase = ''
                mkdir -p $out/bin
                make BUILD_DIR=$TMPDIR PREFIX=$out/bin
              '';
            };
          };
        }
      );
    in
    outputsWithSystem // outputsWithoutSystem;
}
