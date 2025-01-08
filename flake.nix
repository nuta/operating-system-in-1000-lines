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
      {
        packages = {
          default = pkgs.stdenv.mkDerivation {
            version = "1.0.0";
            pname = "dev env";
            src = ./.;

            nativeBuildInputs = with pkgs.buildPackages; [
              llvmPackages.clang-unwrapped
              llvmPackages.bintools-unwrapped
            ];

            # first path will be computed within build env, the second one will be taken from the environment
            installPhase = ''
              mkdir -p $out/bin
              { echo "export PATH=$PATH:\$PATH";
                echo "exec \$SHELL";
              } >> $out/bin/dev-env

              chmod +x $out/bin/dev-env
            '';
          };
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs.buildPackages; [
            llvmPackages.clang-unwrapped
            llvmPackages.bintools-unwrapped
          ];

          shellHook = ''
          '';
        };
      }
    );
}
