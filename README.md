biew
====

[![asciicast](https://asciinema.org/a/792059.svg)](https://asciinema.org/a/792059)

> This repository was converted from the [original CVS repository on SourceForge](https://sourceforge.net/projects/beye/)

BIEW (Binary vIEW), also known as BEYE (Binary EYE), is an ancient hex editor created by Nickols Kurshev circa 1994. Check its [Wikipedia entry](https://en.wikipedia.org/wiki/Beye) for more details.

I made minimum patches, so it can compile nowadays. Tested on Ubuntu 25.10.

## Compile

```shell
git clone https://github.com/merces/biew
cd biew
./configure
make -j $(nproc)
```

## Run

```shell
TERM=vt100 ./biew FILE
```
