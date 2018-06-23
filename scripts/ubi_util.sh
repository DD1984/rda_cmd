#!/bin/sh

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

	if [ ! -e $IN_FILE ]
	then
		echo "image $IN_FILE does not exist"
		return 1
	fi

	if [ ! -e $MOUNT_POINT ]
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

	if [ -e /dev/ubi0_0 ]
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

	if [ -e /dev/ubi0 ]
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

	if [ -n "$MTD_DEV" ]
	then
		echo "nandsim already loaded - erasing $MTD_DEV"
		flash_eraseall /dev/$MTD_DEV > /dev/null 2>&1
	else
		echo -n "loadind nandsim ... "
		modprobe nandsim id_bytes=0x98,0xdc,0x90,0x26

		MTD_DEV=$(get_mtd_dev)
		if [ -z "$MTD_DEV" ]
		then
			echo "failed"
			return 1
		fi
		
		echo "DONE - $MTD_DEV"
	fi

	echo -n "loading image $IN_FILE to $MTD_DEV ... "
	dd if=$IN_FILE of=/dev/$MTD_DEV bs=4096 > /dev/null 2>&1
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
	mount /dev/ubi0_0 $MOUNT_POINT -t ubifs > /dev/null 2>&1

	if [ $? -ne 0 ]
	then
		echo "failed"
		return 1
	fi
	echo "DONE"

	return 0
}

CFG=/tmp/ubinize_cfg.ini
UBIFS_IMG=/tmp/ubifs.img

create_ubinize_cfg () {
	rm -rf $1
	echo "[rootfs-volume]" >> $1
	echo "mode=ubi" >> $1
	echo "image=$UBIFS_IMG"  >> $1
	echo "vol_id=0" >> $1
	echo "vol_type=dynamic" >> $1
	echo "vol_name=rootfs" >> $1
	echo "vol_flags=autoresize" >> $1
}

create_ubi () {
	DIR=$1
	TARGET=$2

	create_ubinize_cfg $CFG

	if [ ! -d $DIR ]
	then
		echo "$DIR - directory does not exist"
		return 1
	fi

	rm -rf $UBIFS_IMG
	rm -rf $TARGET

	echo -n "creating ubifs from $DIR ... "
	mkfs.ubifs -r $DIR -m 4KiB -e 248KiB -x lzo -c 9999 -F -o $UBIFS_IMG > /dev/null 2>&1
	if [ $? -ne 0 ]
	then
		echo "failed"
		return 1
	fi
	echo "DONE"

	echo -n "ubinize to $TARGET ... "
	ubinize -p 256KiB -m 4KiB -s 4KiB -O 4096 -o $TARGET $CFG  > /dev/null 2>&1
	if [ $? -ne 0 ]
	then
		echo "failed"
		return 1
	fi
	echo "DONE"

	rm -rf $UBIFS_IMG
	rm -rf $CFG

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

	echo "Mounting $2 to $3:"

	mount_ubi $2 $3
	if [ $? -ne 0 ]
	then
		echo "mounting FAILED"
		exit 1
	fi
	echo "mounting SUCCEED"

elif [ $1 = -c ]
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

	echo "Creating UBI image"
	create_ubi $2 $3
	if [ $? -ne 0 ]
	then
		echo "creating ubi image FAILED"
		exit 1
	fi
	echo "creating $3 SUCCEED"

elif [ $1 = -r ]
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
	if [ ! -e $2 ]
	then
		echo "file $2 does not exist"
		exit 1
	fi

	echo "Repacking image"

	TMP_MOUNT_POINT=/tmp/ubi_tmp_mnt

	mkdir -p $TMP_MOUNT_POINT 
	#> /dev/null 2>&1

	mount_ubi $2 $TMP_MOUNT_POINT
	if [ $? -eq 0 ]
	then
		create_ubi $TMP_MOUNT_POINT $3
		result=$?

		umount $TMP_MOUNT_POINT > /dev/null 2>&1
		ubidetach -d 0 > /dev/null 2>&1
		rm -rf $TMP_MOUNT_POINT

		if [ $result -eq 0 ]
		then
			echo -n "SUCCEED - size before: "
			echo -n `ls -lh $2 | awk '{print $5}'`
			echo -n " size after: "
			echo -n `ls -lh $3 | awk '{print $5}'`
			echo
		else
		echo "repacking failed"
		fi

	else
		echo "mounting $2 failed"
		exit 1
	fi

else
 usage
 exit 1
fi

exit 0
