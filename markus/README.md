# Experimental Artefact for MarkUs: Drop-in use-after-free prevention for low-level languages
This is a prototype allocator for the prevention of use-after-free attacks, as described in the S&amp;P 2020 Paper - "MarkUs: Drop-in use-after-free prevention for low-level languages" by S. Ainsworth and T. M. Jones.

It is implemented as a modification of the Boehm-Demers-Weiser garbage collector, and is distributed under the same license (license.txt).

## Compiling and Installing the Allocator

This project depends on automake, autoconf and libtool, which should be installed before proceeding. The allocator should be configured using the settings given in setup.sh, which should be run from the root of this respository. To change where it is installed, alter "--prefix=~/markus-allocator" to your choice of folder.

## Using the Allocator

To replace malloc for all dynamically linked binaries running within a shell, use 

```
export LD_PRELOAD="$HOME/markus-allocator/lib/libgc.so $HOME/markus-allocator/lib/libgccpp.so"
```

For a single application, you can also define this variable within the execution command, e.g.

```
LD_PRELOAD="$HOME/markus-allocator/lib/libgc.so $HOME/markus-allocator/lib/libgccpp.so" vi README.md
```

## Modifying the Allocator

Many of the features of MarkUs are implemented in freeing_list.c and include/private/freeing_list.h. To add or remove features from MarkUs, or reconfigure its memory overhead, alter the values in freeing_list.h, then clean and reinstall from the bdwgc-markus directory:

```
make clean
make install
```

allchblk.c also features modifications to support several of MarkUs' features, as does alloc.c.
