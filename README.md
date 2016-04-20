# AutoCut

## Building from git on a recent Debian/Ubuntu

    $ sudo apt-get install autoconf automake
    $ git clone https://github.com/johang/autocut.git autocut
    $ cd autocut
    $ autoreconf -i
    $ ./configure
    $ make

And optionally, if you want to install it:

    $ sudo make install
