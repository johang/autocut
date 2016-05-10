# AutoCut

## Building from git on a recent Debian/Ubuntu

    $ sudo apt-get install autoconf automake libglib2.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
    $ git clone https://github.com/johang/autocut.git autocut
    $ cd autocut
    $ autoreconf -i
    $ ./configure
    $ make

And optionally, if you want to install it:

    $ sudo make install

To be able to use it, you probably need some codecs too:

    $ sudo apt-get install imagemagick gstreamer1.0-plugins-good gstreamer1.0-libav libav-tools
