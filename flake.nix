{
  description = "build environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/24.05";

  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs =
    { self
    , nixpkgs
    , flake-utils
    ,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      rec {
        packages = {
          default = pkgs.stdenv.mkDerivation {
            version = "1.0.0";
            pname = "dev env";
            src = ./.;

            propagatedBuildInputs = with pkgs; [
              llvmPackages.clangUseLLVM
              llvmPackages.bintools-unwrapped
            ];

            buildPhase = ''

            '';

            installPhase = ''
              mkdir -p $out
              touch $out/.dummy
            '';
          };
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs.buildPackages; [
            llvmPackages.clangUseLLVM
            llvmPackages.bintools-unwrapped
          ];

          shellHook = ''
          '';
        };
      }
    );
}
