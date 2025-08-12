# Little FS Test program


```
make my_test && ./my_test fs.bin
```

fs.bin is a dump of the file system on a Particle device.

Using DFU (update USB PID from d019 if not a Boron):

```
dfu-util -d 2b04:d019 -a 2 -s 0x80000000:0x200000 -U fs.bin
```

Using Segger J/Link and nrfjprog:

```
nrfjprog --readqspi qspi.bin
dd bs=1024 count=2048 if=qspi.bin of=fs.bin
```
