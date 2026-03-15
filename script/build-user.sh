#! /bin/bash

FULLTARGET=${2}-${1}
if [ ${3} == 1 ] # This is a generic target
then
	FULLTARGET=${2}-all
fi

#edit to add system user

cd ${ROOT_DIR}/user
if [ ! -d ${FULLTARGET} ]
then
	echo "Usermode program '${FULLTARGET}' does not exist!"
	exit 1
fi

# If the target has its own Makefile, invoke it directly rather than going
# through the shared user/Makefile (which only handles generic targets).
if [ -f ${FULLTARGET}/Makefile ]
then
	make -C ${FULLTARGET} ARCH=${1}
else
	make TARGET=${2} ARCH=${1} TARGET_ROOT=${FULLTARGET}
fi

if [ $? != 0 ]
then
	echo "Compilation for '${1}' failed!"
	exit 1
fi
