# CPM-Emulator

[![License][license-image]][license-url]
[![Build][build-image]][build-url]
[![Issues][issues-image]][issues-url]

> Yet another Z80 Computer with CP/M capabilities (no BIOS, emulated BDOS, executed CP/M)

A C++ CP/M machine emulator with embedded [Z80 by Sainz de Baranda y Goñi, Manuel <manuel@zxe.io>](https://github.com/redcode/Z80). BDOS functions are C++ coded. BIOS functions are not provided (until now).

Tested with ZEXDOC from http://mdfs.net/Software/Z80/Exerciser/

<figure>
  <img src="CPM_2-2.jpg" alt="CPM 2.2 screenshot" width="400px" style="width:200px;"/>
  <figcaption>Par <a href="//commons.wikimedia.org/wiki/User:Mspecht" title="User:Mspecht">Mspecht</a> — <span class="int-own-work" lang="fr">Travail personnel</span>, <a href="https://creativecommons.org/licenses/by-sa/3.0" title="Creative Commons Attribution-Share Alike 3.0">CC BY-SA 3.0</a>, <a href="https://commons.wikimedia.org/w/index.php?curid=16374288">Lien</a></figcaption>
</figure>
  
## Installation

Copy localy the CCP-DR.64K file from https://github.com/MockbaTheBorg/RunCPM/tree/master/CCP
in the directory you start the emulator from.

Create dir A, B, _etc._ (up to P) as you need. They will simulate local CP/M disks. Only files with 8+3 filename will be seenable, whatever their case. Try `DIR a:`.

You may also use [GlassTTY: TrueType VT220 font](https://github.com/svofski/glasstty).

## Usage example

### Start emulator & load CP/M (as default)
```sh
$ cpm 
```
The console is a Kaypro/ADM-3A like terminal (`^Z` clear screen, `ESC = r c` cursor position...). `Ctrl-C` goes to CP/M (warm boot); `Ctrl-]` then `q` leaves the emulator (`Ctrl-]` twice sends a `Ctrl-]`).

### Start emulator, load & run a program
The arguments are given to the program as its command line; the emulator stops when the program ends.
```sh
$ cpm zexdoc.com
$ cpm A/DUMP.COM B:FILE.TXT

Zilog Z80 CPU Emulator
Copyright (c) 1999-2018 Manuel Sainz de Baranda y Goni.
Released under the terms of the GNU General Public License v3.

CPM 2.2 Emulator - Copyright (c) 2021 by M. Sibert

Z80doc instruction exerciser
<adc,sbc> hl,<bc,de,hl,sp>....  OK

```

Input can be piped (`printf 'DIR\n' | cpm`): the emulator stops at the end of input.

<!--
A few motivating and useful examples of how your product can be used. Spice this up with code blocks and potentially more screenshots.

_For more examples and usage, please refer to the [Wiki][wiki]._
-->

## Development setup

Needs CMake, and a C++17 compiler. The Z80 emulator https://github.com/redcode/Z80 ([licence GNU Public Licence v3](http://www.gnu.org/copyleft/gpl.html)) and its Zeta library are Git submodules.

```sh
git clone --recursive https://github.com/Marcussacapuces91/CPM-Emulator.git
cd CPM-Emulator
cmake -S . -B build
cmake --build build
```

In an existing clone, get the submodules with `git submodule update --init`.

The console uses POSIX terminal functions (Linux, macOS).

## Release History

* 0.3
    * CHANGE: Build with CMake, Z80 as a submodule ;
    * All CP/M 2.2 BDOS functions (0-40) & BIOS entries ;
    * Console in raw mode, Kaypro escape sequences ;
    * Passes the ZEXDOC tests.
* 0.2
    * CHANGE: Remplacement par le moteur https://github.com/redcode/Z80 ;
    * CHANGE: Reprise de l'affichage des mnemonics ;
    * Passe les tests ZEXDOC (presque).
* 0.1
    * Premières fonctions BDOS et moteur Z80 perso.
    * Echec des tests ZEXDOC.

## Meta

Marc SIBERT – [@LabAllen91](https://twitter.com/LabAllen91) – contact@dispositifs.fr

Distributed under the Apache 2.0 license. See [``LICENSE``](https://github.com/Marcussacapuces91/CPM-Emulator/blob/main/LICENSE) for more information.

[https://github.com/Marcussacapuces91/CPM-Emulator](https://github.com/Marcussacapuces91/CPM-Emulator)

## Contributing

1. Fork it (<https://github.com/Marcussacapuces91/CPM-Emulator/fork>)
2. Create your feature branch (`git checkout -b feature/fooBar`)
3. Commit your changes (`git commit -am 'Add some fooBar'`)
4. Push to the branch (`git push origin feature/fooBar`)
5. Create a new Pull Request

## References


* https://github.com/joelang/z80-sbc: Source code of [cpm22.lst](https://raw.githubusercontent.com/joelang/z80-sbc/master/cpm22.lst)
* BDOS functions & explanations: http://seasip.info/Cpm/bdos.html
* CP/M FCB: http://seasip.info/Cpm/fcb.html
* Z80 Instruction Exerciser: http://mdfs.net/Software/Z80/Exerciser/
* Z80 Flags Affection: http://www.z80.info/z80sflag.htm
* Z80 instruction set: https://clrhome.org/table/
* Soul of CP/M, Mitchell Waite & Robert Lafore https://datassette.nyc3.cdn.digitaloceanspaces.com/livros/soul_of_cp-m.pdf

<!-- Markdown link & img dfn's -->
[license-image]: https://img.shields.io/github/license/Marcussacapuces91/CPM-Emulator?label=Licence
[license-url]: LICENSE

[build-image]: https://github.com/Marcussacapuces91/CPM-Emulator/actions/workflows/build.yml/badge.svg
[build-url]: https://github.com/Marcussacapuces91/CPM-Emulator/actions/workflows/build.yml

[issues-image]: https://img.shields.io/github/issues/Marcussacapuces91/CPM-Emulator?label=Issues
[issues-url]: https://github.com/Marcussacapuces91/CPM-Emulator/issues
