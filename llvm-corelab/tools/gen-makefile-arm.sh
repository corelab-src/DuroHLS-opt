#!/bin/sh
TOOLS_DIR=$LLVM_CORELAB_INSTALL_DIR/tools

TOOLS_DIRS=`ls -l $TOOLS_DIR | egrep '^d' | awk '{print $9}'`
#TOOLS_DIRS=`find $TOOLS_DIR -maxdepth 1 -type d -exec basename {} \;`

echo "\nHello, I'm bash"
echo "\nBash: I'm gonna make a Makefile.arm for below tools' names"
for DIR in $TOOLS_DIRS
do
  echo  ${DIR}
done

cp Makefile.arm $LLVM_CORELAB_INSTALL_DIR/
touch $TOOLS_DIR/Makefile.arm
echo "DIRS=\\" > $TOOLS_DIR/Makefile.arm
for DIR in $TOOLS_DIRS
do
  echo "${DIR} \\" >> $TOOLS_DIR/Makefile.arm
done
echo "
all:
	@for dir in \$(DIRS); do \\
		(cd \$\$dir; make -f Makefile.arm) done;

clean:
	@for dir in \$(DIRS); do \\
		(cd \$\$dir; make -f Makefile.arm clean) done;
" >> $TOOLS_DIR/Makefile.arm

for DIR in $TOOLS_DIRS
do
  touch $TOOLS_DIR/$DIR/Makefile.arm
  echo "TOOL_NAME=\$(shell basename \$(CURDIR))

include ../../Makefile.arm" > $TOOLS_DIR/$DIR/Makefile.arm
done
echo "\nDone.\n"
