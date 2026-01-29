This repo contains the core components of the Ampersand system. Most
of the code lives here.

[Most of the documentation is here](https://mackinnon.info/ampersand/).

PRs are considered on onto the develop branch.

Code Metrics
============

        cloc --vcs=git --exclude-list-file=.clocignore .


Building Piper TSS
==================

        https://github.com/OHF-Voice/piper1-gpl.git
        cd piper1-gpl
        cd libpiper
        cmake -Bbuild -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/install
        cmake --build build
        cmake --install build
        # Moved the libpiper.so into the lib directory along with the other .so files
        # make a libpiper-${ARCH}.tar.gz file that has include, lib, and the espeak-ng-data directories

Useful command (Windows)

        scp .\en_US-amy-low.onnx admin@amp-hub:/home/admin
        