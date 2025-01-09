{ pkgs, repo, ... }:

pkgs.stdenv.mkDerivation {
  name = "powermon";
  version = "0.1.0";

  buildInputs = with pkgs; [ clang pkg-config systemdLibs ];

  src = repo;
  PROJECT_DIR = repo;

  buildPhase = ''
    ls
    echo $PWD
    make BUILD_DIR=$TMPDIR PREFIX=$out
  '';

}
