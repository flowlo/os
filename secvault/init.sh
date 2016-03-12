#/bin/sh

BASEDIR="tmp"
MODULE="secvault"
DEV_PREFIX="sv_data"
MODE="666"
MAJOR=231

echo "Removing module ..."
/sbin/rmmod ./$MODULE.ko > /dev/null 2>&1

set -e

echo "Removing device nodes ..."
rm -fv /${BASEDIR}/${DEV_PREFIX}[0-3]

echo "Removing control node ..."
rm -fv /${BASEDIR}/sv_ctl

echo "Inserting module ..."
/sbin/insmod ./$MODULE.ko $*

echo "Recreating control node ..."
mknod /${BASEDIR}/sv_ctl c $MAJOR 0

echo "Recreating device nodes ..."
mknod /${BASEDIR}/${DEV_PREFIX}0 c $MAJOR 1
mknod /${BASEDIR}/${DEV_PREFIX}1 c $MAJOR 2
mknod /${BASEDIR}/${DEV_PREFIX}2 c $MAJOR 3
mknod /${BASEDIR}/${DEV_PREFIX}3 c $MAJOR 4

GROUP=$([ $(getent group staff) ] && echo 'staff' || echo 'wheel')

chgrp $GROUP /${BASEDIR}/${DEV_PREFIX}[0-3]
chmod $MODE /${BASEDIR}/${DEV_PREFIX}[0-3]

chgrp $GROUP /${BASEDIR}/sv_ctl
chmod $MODE /${BASEDIR}/sv_ctl
