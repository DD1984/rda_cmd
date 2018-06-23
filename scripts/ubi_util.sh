#!/bin/sh

PAGE_SIZE=4096

is_module_loaded () {
	for MOD in `cat /proc/modules | awk '{print $1}'`
	do
		if [ $MOD = $1 ]
		then
			return 1
		fi
	done

	return 0
}

is_file_exist () {
	if test -e $1
	then
		return 1
	fi

	return 0
}

get_mtd_dev () {
	echo `cat /proc/mtd | grep "NAND simulator partition 0" | awk -F ":" '{print $1}'`
}

usage () {
	echo "usage:"
	echo "    -m [image] [mounting point] - mount ubi image"
	echo "    -c [dir] [target image name] - create imate from directory [dir]"
	echo "    -r [image name] [repack image name] - repack ubi image for decrease size"
	echo
}

mount_ubi () {
	IN_FILE=$1
	MOUNT_POINT=$2

	is_file_exist $IN_FILE
	if [ $? -ne 1 ]
	then
		echo "image $IN_FILE does not exist"
		return 1
	fi

	is_file_exist $MOUNT_POINT
	if [ $? -ne 1 ]
	then
		echo "mounting point $MOUNT_POINT does not exist"
		return 1
	fi

	is_module_loaded ubi
	if [ $? -eq 0 ]
	then
		echo "loading ubi"
		modprobe ubi
	fi

	is_module_loaded ubifs
	if [ $? -eq 0 ]
	then
		echo "loading ubifs"
		modprobe ubifs
	fi

	is_file_exist /dev/ubi0_0
	if [ $? -eq 1 ]
	then
		cat /proc/mounts | grep /dev/ubi0_0 > /dev/null 2>&1
		if [ $? -eq 0 ]
		then
			echo -n "ubi0_0 mounted - trying umount ... "
			umount /dev/ubi0_0 > /dev/null 2>&1
			if [ $? -ne 0 ]
			then
				echo "failed"
				return 1
			fi
			echo "DONE"
		fi
	fi

	is_file_exist /dev/ubi0
	if [ $? -eq 1 ]
	then
		echo -n "ubi0 already existed - trying detach ... "
		ubidetach -d 0 > /dev/null 2>&1
		if [ $? -ne 0 ]
		then
			echo "failed"
			return 1
		fi
		echo "DONE"
	fi

	MTD_DEV=$(get_mtd_dev)

	if test -n "$MTD_DEV"
	then
		echo "nandsim already loaded - erasing $MTD_DEV"
		flash_eraseall /dev/$MTD_DEV > /dev/null 2>&1
	else
		echo -n "loadind nandsim ... "
		modprobe nandsim id_bytes=0x98,0xdc,0x90,0x26

		MTD_DEV=$(get_mtd_dev)
		if test -z "$MTD_DEV"
		then
			echo "failed"
			return 1
		fi
		
		echo "DONE - $MTD_DEV"
	fi

	echo -n "loading image to $MTD_DEV ... "
	dd if=$IN_FILE of=/dev/$MTD_DEV bs=$PAGE_SIZE > /dev/null 2>&1
	echo "DONE"

	echo -n "attaching $MTD_DEV to ubi0 ... "
	ubiattach -m `echo $MTD_DEV | grep -o [0-9]*` -d 0 -O 4096 > /dev/null 2>&1
	if [ $? -ne 0 ]
	then
		echo "failed"
		return 1
	fi
	echo "DONE"

	echo -n "mounting ubi0_0 to $MOUNT_POINT ... "
	mount /dev/ubi0_0 /mnt -t ubifs > /dev/null 2>&1

	if [ $? -ne 0 ]
	then
		echo "failed"
		return 1
	fi
	echo "DONE"

	return 0
}

if [ $1 = -m ]
then
	if [ -z $2 ]
	then
		usage
		exit 1
	fi
	if [ -z $3 ]
	then
		usage
		exit 1
	fi

	mount_ubi $2 $3

elif [ $1 = -c ]
then
	echo "creating image"
elif [ $1 = -r ]
then
	echo "repack image"
else
 usage
 exit 1
fi

exit 0
