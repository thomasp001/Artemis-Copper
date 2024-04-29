# CSSE4011 Student Repositories

## Folder Setup

Setup your local workspace as follows, replacing sXXXXXXXX with your student login (s<first 7 digits of student number>)
```
# Install West
pip3 install --user -U west
# Create a folder for this course
mkdir -p ~/csse4011
cd ~/csse4011
# Clone your student git repository into the folder:
git clone https://source.eait.uq.edu.au/git/csse4011-sXXXXXXX csse4011-sXXXXXXXX
# Tell West what the main repo is
west init -l csse4011-sXXXXXXXX
# Pull down all the required repositories
west update
```

## Toolchain Setup

Follow the getting started guide to install the required toolchains.
https://docs.zephyrproject.org/latest/getting_started/index.html


## Build an Application

```
west build -b thingy52_nrf52832 csse4011-sXXXXXXX/apps/p1/blinky
```
