#
##
# Sergej Schumilo, 2019 <sergej@schumilo.de>
# Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
./configure --target-list=i386-softmmu,x86_64-softmmu \
	--enable-gtk --enable-vnc --enable-pt \
	--disable-werror --disable-debug-info
make -j8
