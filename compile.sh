#
# This file is part of Redqueen.
#
# Sergej Schumilo, 2019 <sergej@schumilo.de>
# Cornelius Aschermann, 2019 <cornelius.aschermann@rub.de>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
./configure --target-list=i386-softmmu,x86_64-softmmu --enable-gtk --enable-vnc --enable-pt --enable-redqueen --disable-werror
make -j8
