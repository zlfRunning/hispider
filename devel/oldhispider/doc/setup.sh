#!/bin/bash
prefix_path="/usr/local"
perl -i -p -e "s@/usr/local@$prefix_path@g" rc.hispider rc.hispiderd
install -c -m755  rc.hispider /etc/rc.d/init.d/hispider
chkconfig --add hispider
chkconfig --level 345 hispider on
install -c -m644 rc.hispider.ini /usr/local/etc/hispider.ini

