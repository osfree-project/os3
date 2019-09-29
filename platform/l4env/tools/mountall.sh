#! /bin/sh
#
# map required directories into chroot
#
# Use:
# "./mountall.sh mount <main dir>" to mount all dirs
# "./mountall.sh unmount <main dir>" to unmount them
#
# parameters:
#
# $1: mount|unmount
# $2: <main dir>
#
# Add/remove extra dirs as needed:
dirs="/dev /dev/shm /dev/pts /proc /sys /tmp /mnt /home $2"

case $1 in
mount)
  # mount all subdirs in order
  for dir in $dirs; do
      mount -o bind $dir .$dir
  done
  ;;

unmount)
  # unmount all subdirs in the reverse order
  for dir in `echo $dirs | rev`; do
      umount -R -f .`echo $dir | rev`
  done
  ;;
esac
