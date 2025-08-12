# Little FS Test program

This is a test program that uses the LittleFS implementation in Device OS (actually the same .c and .h files)
to test and dump the contents of a saved binary copy of the file system on a device.

To run:

```
make my_test && ./my_test fs.bin
```

fs.bin is a dump of the file system on a Particle device.

It extracts the contents into the directory `filesystem` in this directory.

## Getting a file system binary file

Using DFU (update USB PID from d019 if not a Boron, the last two hex digits are the platform ID):

```
dfu-util -d 2b04:d019 -a 2 -s 0x80000000:0x200000 -U fs.bin
```

Using Segger J/Link and nrfjprog:

```
nrfjprog --readqspi qspi.bin
dd bs=1024 count=2048 if=qspi.bin of=fs.bin
```

The 0x200000 is 0x400000, and the 2048 is 4096, on the Tracker which has a 4 MB file system instead of 2 MB.
