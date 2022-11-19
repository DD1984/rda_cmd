rda_cmd - program for read/write/erase nand partitions on Orange Pi 2G-IOT ARM (RDA8810) from BootRom

In fullfw writing mode you can write UBIFS images to NAND:  
1. move jumper on the orange pi to NAND and push "power" button on orange pi 3-5 second, then connect to your linux machine via usb
2. check that device named like "ttyACM0" is initialized (`dmesg`)
3. `make` -> in project root should be created "rda_cmd"
4. copy ready fullfw.img to project root:
https://drive.google.com/file/d/1vpSL4UtvFoCQXMovUUAd7WTes9TzZR8a/view fullfw.zip
https://drive.google.com/file/d/1QV_u3UY-4lfz1zKtuF3VsBLi0odVFi8Z/view small_ubuntu_wifi.zip
5. `sudo ./rda_cmd fullfw fullfw.img`
6. restart you orange pi
7. to check that everything is OK you can connect orange pi to PC via usb-ttl cable (`sudo screen /dev/ttyUSB0 921600`) abd try to login: root/orangepi

for this help very thanks to [Dmitry Obrazumov](https://github.com/DmitryOb)
